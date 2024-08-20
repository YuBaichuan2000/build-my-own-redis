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


using namespace std;

const size_t k_max_msg = 4096;

void die(const string &msg) {
    cout << msg << endl;
    exit(EXIT_FAILURE);
}

void msg(const string &msg) {
    cout << msg << endl;
}

static int32_t read_full(int fd, char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv; 
        buf += rv;
    }
    return 0;
}

static int32_t write_all(int fd, const char *buf, size_t n) {
    while (n > 0){
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0) {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

// // client makes requests and receive responses
// static int32_t query(int fd, const char* text) {
//     uint32_t len = (uint32_t) strlen(text);

//     // check if message over limit
//     if (len > k_max_msg) {
//         return -1;
//     }

//     // copy msg to write buffer and send request
//     char wbuf[4 + k_max_msg];
//     memcpy(wbuf, &len, 4);
//     memcpy(&wbuf[4], text, len);

//     if (int32_t err = write_all(fd, wbuf, 4+len)) {
//         return err;
//     }

//     // receive response from server and process
//     char rbuf[4 + k_max_msg + 1];
//     errno = 0;
//     int32_t err = read_full(fd, rbuf, 4);
//     if (err) {
//         if (errno == 0) {
//             msg("EOF");
//         } else {
//             msg("read() error");
//         }
//         return err;
//     }
    
//     // check if response too long
//     memcpy(&len, rbuf, 4);
//     if (len > k_max_msg) {
//         msg("too long");
//         return -1;
//     }

//     // process response body
//     err = read_full(fd, &rbuf[4], len);
//     if (err) {
//         msg("read() error");
//         return err;
//     }

//     rbuf[4 + len] = '\0';
//     cout << "server says: " << &rbuf[4] << endl;
//     return 0;
// }

static int32_t send_req(int fd, const vector<string> &cmd) {
    uint32_t len = 4;
    for (const string &s : cmd){
        len += 4 + s.size();
    }
    if (len > k_max_msg) {
        return -1;
    }

    char wbuf[4 + k_max_msg];
    memcpy(&wbuf[0], &len, 4);

    uint32_t n = cmd.size();
    memcpy(&wbuf[4], &n, 4);
    size_t cur = 8;

    for (const string &s : cmd){
        uint32_t p = (uint32_t)s.size();
        memcpy(&wbuf[cur], &p, 4);
        memcpy(&wbuf[cur + 4], s.data(), s.size());
        cur += 4 + s.size();
    }
    return write_all(fd, wbuf, 4+len);
}

static int32_t read_res(int fd) {
    uint32_t len;
    // receive response from server and process
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    int32_t err = read_full(fd, rbuf, 4);
    if (err) {
        if (errno == 0) {
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }
    
    // check if response too long
    memcpy(&len, rbuf, 4);
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }

    // process response body
    err = read_full(fd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }
    

    // print the result
    uint32_t rescode = 0;
    if (len < 4) {
        msg("bad response");
        return -1;
    }
    memcpy(&rescode, &rbuf[4], 4);
    
    printf("server says: [%u] %.*s\n", rescode, len - 4, &rbuf[8]);
    return 0;
}

int main(int argc, char **argv) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket()");
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);  // use htons to set the port correctly
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // use htonl with INADDR_ANY for all interfaces

    int rv = connect(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));

    if (rv) {
        die("connect");
    }

    std::vector<std::string> cmd;
    for (int i = 1; i < argc; ++i) {
        cmd.push_back(argv[i]);
    }
    int32_t err = send_req(fd, cmd);
    if (err) {
        goto L_DONE;
    }
    err = read_res(fd);
    if (err) {
        goto L_DONE;
    }

L_DONE:
    close(fd);
    return 0;
}


