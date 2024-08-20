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
#include <map>


using namespace std;

// maximum msg length
const size_t k_max_msg = 4096;

// connection states
enum {
    STATE_REQ = 0, // request
    STATE_RES = 1, // response
    STATE_END = 2, // remove
};

// connection metadata
struct Conn {
    int fd = -1;
    uint32_t state = 0;
    size_t rbuf_size = 0; // read buffer
    uint8_t rbuf[4 + k_max_msg]; // client request
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

static void fd_set_nb(int fd) {
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno) {
        die("fcntl error");
        return;
    }

    flags |= O_NONBLOCK;

    errno = 0;
    (void)fcntl(fd, F_SETFL, flags);
    if (errno) {
        die("fcntl error");
    }
}

// put conn into collection
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

static void state_req(Conn *conn);
static void state_res(Conn *conn);

// parse commands
static int32_t parse_req(const uint8_t *data, size_t len, vector<string>& out){
    if (len < 4){
        return -1;
    }
    uint32_t n = 0;
    memcpy(&n, &data[0], 4);
    if (n > k_max_msg) {
        return -1;
    }

    size_t pos = 4;
    while (n--) {
        if (pos + 4 > len) {
            return -1;
        }
        uint32_t sz = 0;
        memcpy(&sz, &data[pos], 4);
        if (pos + 4 + sz > len){
            return -1;
        }
        out.push_back(string((char*)&data[pos+4], sz));
        pos += 4 + sz;
    }

    if (pos != len) {
        return -1;
    }
    return 0;
}

enum {
    RES_OK = 0,
    RES_ERR = 1,
    RES_NX = 2,
};

static std::map<std::string, std::string> g_map;

static uint32_t do_get(const vector<string> &cmd, uint8_t *res, uint32_t *reslen){
    // look up key cmd[1] in the hashtable
    if (!g_map.count(cmd[1])){
        return RES_NX;
    }
    string &val = g_map[cmd[1]];
    assert(val.size() <= k_max_msg);
    memcpy(res, val.data(), val.size());
    *reslen = (uint32_t) val.size();
    return RES_OK;
}

static uint32_t do_set(const vector<string> &cmd, uint8_t *res, uint32_t *reslen) {
    (void) res;
    (void) reslen;
    g_map[cmd[1]] = cmd[2];
    return RES_OK;
}

static uint32_t do_del(
    const std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
    (void)res;
    (void)reslen;
    g_map.erase(cmd[1]);
    return RES_OK;
}

static bool cmd_is(const std::string &word, const char *cmd) {
    return 0 == strcasecmp(word.c_str(), cmd);
}

// handle GET, SET, DEL
static int32_t do_request(const uint8_t *req, uint32_t reqlen, uint32_t* rescode, uint8_t* res, uint32_t* reslen){
    vector<string> cmd;
    if (0 != parse_req(req, reqlen, cmd)) {
        msg("bad req");
        return -1;
    }
    if (cmd.size() == 2 && cmd_is(cmd[0], "get")) {
        *rescode = do_get(cmd, res, reslen);
    } else if (cmd.size() == 3 && cmd_is(cmd[0], "set")){
        *rescode = do_set(cmd, res, reslen);
    } else if(cmd.size() == 2 && cmd_is(cmd[0], "del")){
        *rescode = do_del(cmd, res, reslen);
    } else {
        // cmd is not recognized
        *rescode = RES_ERR;
        const char *msg = "Unknown cmd";
        strcpy((char *)res, msg);
        *reslen = strlen(msg);
        return 0;
    }
    return 0;
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
    if (4 + len > conn->rbuf_size) {
        // not all request data received in the buffer
        return false;
    }

    // got a full request msg
    printf("client says: %.*s\n", len, &conn->rbuf[4]);


    // generate response
    uint32_t rescode = 0;
    uint32_t wlen = 0;
    int32_t err = do_request(&conn -> rbuf[4], len, &rescode, &conn->wbuf[4+4], &wlen);
    
    if (err) {
        conn->state = STATE_END;
        return false;
    }
    wlen += 4;
    memcpy(&conn -> wbuf[0], &wlen, 4);
    memcpy(&conn -> wbuf[4], &rescode, 4);
    conn -> wbuf_size = 4 + wlen;

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

static bool try_fill_buffer(Conn *conn) {
    // try filling the buffer
    assert(conn -> rbuf_size < sizeof(conn -> rbuf)); // sanity check, buffer not overfilled
    ssize_t rv = 0;
    do {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size; // check how much space left in buffer
        rv = read(conn -> fd, &conn->rbuf[conn->rbuf_size], cap); // keep appending till whole request and get processed 
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




static bool try_flush_buffer(Conn* conn) {
    ssize_t rv = 0;
    do {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;
        rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
    } while (rv<0 && errno == EINTR);
    if (rv < 0 && errno == EAGAIN) {
        return false;
    }
    if (rv < 0) {
        msg("write() error");
        conn -> state = STATE_END;
        return false;
    }
    conn ->wbuf_sent += (size_t) rv;
    assert(conn->wbuf_sent == conn->wbuf_size);
    if (conn ->wbuf_sent == conn->wbuf_size) {
        conn->state = STATE_REQ;
        conn->wbuf_sent = 0; 
        conn->wbuf_size = 0;
        return false;
    }
    return true;

}

// writer state
static void state_res(Conn* conn) {
    while (try_flush_buffer(conn)) {}
}

// reader state
static void state_req(Conn *conn) {
    while (try_fill_buffer(conn)){}
}


// state machine for client connecions
static void connection_io(Conn *conn) {
    if (conn -> state == STATE_REQ){
        state_req(conn);
    } else if (conn -> state == STATE_RES) {
        state_res(conn);
    } else {
        assert(0);
    }
}

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
        // try to accept a new connection if the listening fd is active
        if (poll_args[0].revents) {
            (void)accept_new_conn(fd2conn, fd);
        }
    }
    
    return 0;
}
