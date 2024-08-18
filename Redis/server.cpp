#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <cassert>
#include <fcntl.h>
#include <poll.h>


using namespace std;

const size_t k_max_msg = 4096;

enum {
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2,
};

struct Conn {
    int fd = -1;
    uint32_t state = 0;
    size_t rbuf_size = 0;
    uint8_t rubf[4 + k_max_msg];
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4 + k_max_msg];
};

void die(const string &msg) {
    cout << msg << endl;
    exit(EXIT_FAILURE);
}

void msg(const string &msg) {
    cout << msg << endl;
}

// static int32_t read_full(int fd, char *buf, size_t n) {
//     while (n > 0) {
//         ssize_t rv = read(fd, buf, n);
//         if (rv <= 0) {
//             return -1;
//         }
//         assert((size_t)rv <= n);
//         n -= (size_t)rv; 
//         buf += rv;
//     }
//     return 0;
// }

// static int32_t write_all(int fd, const char *buf, size_t n) {
//     while (n > 0){
//         ssize_t rv = write(fd, buf, n);
//         if (rv <= 0) {
//             return -1;
//         }
//         assert((size_t)rv <= n);
//         n -= (size_t)rv;
//         buf += rv;
//     }
//     return 0;
// }

static int32_t one_request(int connfd) {
    // 4 bytes header
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    int32_t err = read_full(connfd, rbuf, 4);
    if (err) {
        if (errno == 0) {
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, 4);
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }

    // request body
    err = read_full(connfd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    rbuf[4 + len] = '\0';
    printf("client says: %s\n", &rbuf[4]);

    // reply
    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    len = (uint32_t) strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);
    return write_all(connfd, wbuf, 4 + len);
}

static void conn_put(vector<Conn*> &fd2conn, struct Conn *conn) {
    if (fd2conn.size() <= (size_t)conn->fd) {
        fd2conn.resize(conn->fd+1);
    }
    fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(vector<Conn*> &fd2conn, int fd){
    // accept
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr*) &client_addr, &socklen);
    if (connfd < 0){
        msg("accept( error)");
        return -1;
    }

    fd_set_nb(connfd);

    struct Conn *conn = (struct Conn*) malloc(sizeof(struct Conn));
    // if no memory available?
    if (!conn) {
        close(connfd);
        return -1;
    }

    conn -> fd = connfd;
    conn -> state = STATE_REQ;
    conn -> rbuf_size = 0;
    conn -> wbuf_size = 0;
    conn -> wbuf_sent = 0;
    conn_put(fd2conn, conn);
    return 0;
}

// state machine for client connecions
static void connectioni_io(Conn *conn) {
    if (conn -> state == STATE_REQ){
        state_req(conn);
    } else if (conn -> state == STATE_RES) {
        state_res(conn);
    } else {
        assert(0);
    }
}

// reader state machine
static void state_req(Conn *conn) {
    while (try_fill_buffer(conn)){}
}

static bool try_fill_buffer(Conn* conn) {
    // try filliing the buffer
    assert(conn -> rbuf_size < sizeof(conn -> rbuf));
    ssize_t rv = 0
    do {
        size_t cap = sizeof(conn -> rbuf) - conn -> rbuf_size;
        rv = read(conn -> fd, &conn->rbuf[conn->rbuf_size], cap);
    } while (rv <0 && errno == EINTR);
    if (rv <0 && errno == EAGAIN) {
        return false;
    }
    if (rv < 0) {
        msg("read( error)");
        conn -> state = STATE_END;
        return false;
    }
    if (rv == 0){
        if (conn ->rbuf_size > 0){
            msg("unexpected EOF");
        } else {
            msg("EOF");
        }
        conn->state = STATE_END;
        return false;
    }

    conn -> rbuf_size += (size_t) rv;
    assert(conn -> rbuf_size <= sizeof(conn -> rbuf));

    // try to process requests one by one
    while (try_one_request(conn)){}
    return (conn -> state == STATE_REQ);
}

static bool try_one_request(Conn *conn) {
    // parse a request from buffer
    // not enough data
    if (conn -> rbuf_size < 4){
        return false;
    }
    uint32_t len = 0;

    memcpy(&len, &conn->rbuf[0], 4);
    if (len > k_max_msg) {
        msg("too long");
        conn->state = STATE_END;
        return false;
    }
    if (4 + len > conn ->rbuf_size) {
        // not all request data received in the buffer
        return false;
    }

    // got a full request msg
    printf("client says: %.*s\n", len, &conn->rbuf[4]);

    // generate response
    memcpy(&conn -> wbuf[0], &len, 4);
    memcpy(&conn -> wbuf[4], &conn ->rbuf[4], len);
    conn -> wbuf_size = 4 + len;

    // remove request from the buffer
    size_t remain = conn -> rbuf_size - 4 - len;
    if (remain) {
        memmove(conn->rbuf, &conn->rbuf[4+len], remain);
    }
    conn->rbuf_size = remain;

    conn -> state = STATE_RES;
    state_res(conn);
    return (conn->state == STATE_REQ);
}

// writer state machine


int main() {
    // obtain a socket handle
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket() error");
    }

    // config the socket
    int val = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) {
        die("setsockopt() error");
    }

    // bind the socket to an addr
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);  // use htons to set the port correctly
    addr.sin_addr.s_addr = htonl(0);  // use htonl with INADDR_ANY for all interfaces

    int rv = bind(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
    if (rv < 0) {
        die("bind() error");
    }

    // listen
    rv = listen(fd, SOMAXCONN);
    if (rv < 0) {
        die("listen() error");
    }

    // a map of all clients connections
    vector<Conn *> fd2conn;

    // set the listen fd to nonblocking mode
    fd_set_nb(fd);

    // event loop
    vector<struct pollfd> poll_args;
    while (true) {
        poll_args.clear();

        // listening socket is pushed first
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);

        // connection fd
        for (Conn* conn : fd2conn) {
            if (!conn) {
                continue;
            }
            struct pollfd pfd = {};
            pfd.fd = conn -> fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events = pfd.events | POLLERR;
            poll_args.push_back(pfd);
        }

        // poll active fds
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
        if (rv < 0){
            die("poll");
        }

        // process active fds
        for (size_t i = 1; i < poll_args.size(); ++i){
            if (poll_args[i].revents) {
                Conn *conn = fd2conn[poll_args[i].fd];
                connection_io(conn);
                if (conn->state == STATE_END) {
                    // client closed, destroy connection
                    fd2conn[conn->fd] = NULL;
                    (void)close(conn->fd);
                    free(conn);
                }
            }
        }




    }


    // accept connections
    // while (true) {
    //     sockaddr_in client_addr = {};
    //     socklen_t addrlen = sizeof(client_addr);
    //     int connfd = accept(fd, reinterpret_cast<sockaddr *>(&client_addr), &addrlen);

    //     if (connfd < 0) {
    //         msg("accept() error");
    //         continue;
    //     }

    //     // serve one client connection at once
    //     while (true) {
    //         int32_t err = one_request(connfd);
    //         if (err) {
    //             break;
    //         }
    //     }

    //     // do_something(connfd);
    //     close(connfd);
    // }

    return 0;
}
