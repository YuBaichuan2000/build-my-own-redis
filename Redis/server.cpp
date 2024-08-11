#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>

using namespace std;

void die(const string &msg) {
    cout << msg << endl;
    exit(EXIT_FAILURE);
}

void msg(const string &msg) {
    cout << msg << endl;
}

// read and write file
void do_something(int connfd) {
    char rbuf[64] = {};
    ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
    if (n < 0) {
        msg("read() error");
        return;
    }
    cout << "client says: " << rbuf << endl;

    string wbuf = "world";
    write(connfd, wbuf.c_str(), wbuf.length());
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

    // accept connections
    while (true) {
        sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);
        int connfd = accept(fd, reinterpret_cast<sockaddr *>(&client_addr), &addrlen);

        if (connfd < 0) {
            msg("accept() error");
            continue;
        }

        do_something(connfd);
        close(connfd);
    }

    return 0;
}
