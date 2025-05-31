#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include <calcLib.h>

#define BUFSIZE     1024
#define BACKLOG     5
#define TIMEOUT_SEC 5

static std::string readLine(int fd) {
    std::string line;
    char c;
    ssize_t n;
    while ((n = recv(fd, &c, 1, 0)) == 1) {
        if (c == '\n') break;
        line.push_back(c);
    }
    if (n <= 0) throw std::runtime_error("connection closed");
    return line;
}
static void sendAll(int fd, const std::string &s) {
    const char *p = s.c_str();
    size_t left = s.size();
    while (left) {
        ssize_t n = send(fd, p, left, 0);
        if (n <= 0) throw std::runtime_error("send failed");
        left -= n;
        p += n;
    }
}
static bool eqFloat(double a, double b) { return std::fabs(a - b) < 0.0001; }

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <bind_addr>:<port>\n";
        return 1;
    }
    std::string arg = argv[1];
    auto colon = arg.find_last_of(':');
    if (colon == std::string::npos) {
        std::cerr << "bad listen argument\n";
        return 1;
    }
    std::string host = arg.substr(0, colon);
    std::string port = arg.substr(colon + 1);

    addrinfo hints{}, *res;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host == "*" ? nullptr : host.c_str(), port.c_str(), &hints, &res) != 0) {
        perror("getaddrinfo");
        return 1;
    }
    int listenfd = -1;
    for (auto p = res; p; p = p->ai_next) {
        listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listenfd == -1) continue;
        int yes = 1;
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(listenfd);
        listenfd = -1;
    }
    freeaddrinfo(res);
    if (listenfd < 0) {
        perror("bind");
        return 1;
    }
    if (listen(listenfd, BACKLOG) != 0) {
        perror("listen");
        return 1;
    }
    std::cout << "Server ready on " << host << ":" << port << '\n';

    initCalcLib();                 // seed libcalc once

    while (true) {                 // one client at a time
        sockaddr_storage peer{};
        socklen_t plen = sizeof peer;
        int fd = accept(listenfd, (sockaddr *)&peer, &plen);
        if (fd < 0) continue;

        try {
            /* set 5 s read timeout */
            timeval tv{TIMEOUT_SEC, 0};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

            /* advertise protocol */
            sendAll(fd, "TEXT TCP 1.0\n\n");

            /* expect OK */
            std::string l = readLine(fd);
            if (l != "OK") {
                close(fd);
                continue;
            }

            /* build assignment */
            char *op = randomType();
            int    i1 = randomInt(), i2 = randomInt(), iref = 0;
            double f1 = randomFloat(), f2 = randomFloat(), fref = 0;
            if (op[0] == 'f') {
                if (strcmp(op, "fdiv") == 0) {
                    while (f2 == 0.0) f2 = randomFloat();
                }
            } else {
                if (strcmp(op, "div") == 0) {
                    while (i2 == 0) i2 = randomInt();
                }
            }

            std::ostringstream msg;
            if (op[0] == 'f') {
                if (strcmp(op, "fadd") == 0)
                    fref = f1 + f2;
                else if (strcmp(op, "fsub") == 0)
                    fref = f1 - f2;
                else if (strcmp(op, "fmul") == 0)
                    fref = f1 * f2;
                else if (strcmp(op, "fdiv") == 0)
                    fref = f1 / f2;

                msg << op << ' ' << std::setprecision(8) << std::scientific << f1 << ' ' << f2
                    << '\n';
            } else {
                if (strcmp(op, "add") == 0)
                    iref = i1 + i2;
                else if (strcmp(op, "sub") == 0)
                    iref = i1 - i2;
                else if (strcmp(op, "mul") == 0)
                    iref = i1 * i2;
                else if (strcmp(op, "div") == 0)
                    iref = i1 / i2;

                msg << op << ' ' << i1 << ' ' << i2 << '\n';
            }
            sendAll(fd, msg.str());

            /* read reply */
            std::string reply = readLine(fd);

            bool ok;
            if (op[0] == 'f') {
                double frep = std::stod(reply);
                ok          = eqFloat(frep, fref);
            } else {
                long long irep = std::stoll(reply);
                ok             = (irep == iref);
            }
            sendAll(fd, ok ? "OK\n" : "ERROR\n");
        } catch (const std::exception &e) {
            sendAll(fd, "ERROR TO\n");
        }
        close(fd);
    }
}

