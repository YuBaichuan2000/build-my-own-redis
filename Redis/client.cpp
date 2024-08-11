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

int main() {
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

    string msg = "hello";
    write(fd, msg.c_str(), msg.length());

    char rbuf[64] = {};
    ssize_t n = read(fd, rbuf, sizeof(rbuf) - 1);
    if (n < 0) {
        die("read");
    }
    printf("server says: %s\n", rbuf);
    close(fd);

    return 0;
}


