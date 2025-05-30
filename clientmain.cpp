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

#define BUFSIZE 1024

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

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <host>:<port>\n";
        return 1;
    }

    /* split host:port */
    std::string arg = argv[1];
    auto colon = arg.find_last_of(':');
    if (colon == std::string::npos) {
        std::cout << "ERROR: RESOLVE ISSUE\n";
        return 1;
    }
    std::string host = arg.substr(0, colon);
    std::string port = arg.substr(colon + 1);

    std::cout << "Host " << host << ", and port " << port << ".\n";

    /* resolve & connect */
    addrinfo hints{}, *res;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0) {
        std::cout << "ERROR: RESOLVE ISSUE\n";
        return 1;
    }
    int fd = -1;
    for (auto p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == -1) continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        std::cout << "ERROR: CANT CONNECT TO " << host << "\n";
        return 1;
    }
#ifdef DEBUG
    std::cout << "Connected.\n";
#endif

    /* 1. protocol lines */
    bool proto_ok = false;
    while (true) {
        std::string l = readLine(fd);
        if (l.empty()) break;                 // blank line marks end
#ifdef DEBUG
        std::cout << "SERVER: " << l << '\n';
#endif
        if (l == "TEXT TCP 1.0") proto_ok = true;
    }
    if (!proto_ok) {
        std::cout << "ERROR: MISSMATCH PROTOCOL\n";
        close(fd);
        return 1;
    }
    sendAll(fd, "OK\n");

    /* 2. assignment */
    std::string assignment = readLine(fd);
    std::cout << "ASSIGNMENT: " << assignment << '\n';

    std::istringstream iss(assignment);
    std::string op;
    double f1, f2;
    int i1, i2;
    iss >> op;
    std::ostringstream reply;
    if (op.front() == 'f') {
        iss >> f1 >> f2;
        double res;
        if (op == "fadd")
            res = f1 + f2;
        else if (op == "fsub")
            res = f1 - f2;
        else if (op == "fmul")
            res = f1 * f2;
        else if (op == "fdiv")
            res = f1 / f2;
        else
            throw std::runtime_error("bad op");

        reply << std::setprecision(8) << std::scientific << res << '\n';
#ifdef DEBUG
        std::cout << "Calculated the result to " << res << '\n';
#endif
    } else {
        iss >> i1 >> i2;
        long long res;
        if (op == "add")
            res = i1 + i2;
        else if (op == "sub")
            res = i1 - i2;
        else if (op == "mul")
            res = i1 * i2;
        else if (op == "div")
            res = i1 / i2;
        else
            throw std::runtime_error("bad op");

        reply << res << '\n';
#ifdef DEBUG
        std::cout << "Calculated the result to " << res << '\n';
#endif
    }
    sendAll(fd, reply.str());

    /* 3. verdict */
    std::string verdict = readLine(fd);
    std::cout << verdict << " (myresult=" << reply.str().substr(0, reply.str().size() - 1)
              << ")\n";

    close(fd);
    return verdict == "OK" ? 0 : 2;
}
