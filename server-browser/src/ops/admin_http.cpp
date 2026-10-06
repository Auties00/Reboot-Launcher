#include "ops/admin_http.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>

namespace sb::ops {

std::pair<std::string, int> split_host_port(const std::string& s) {
    const auto colon = s.rfind(':');
    if (colon == std::string::npos) throw std::runtime_error("expected host:port, got " + s);
    std::string host = s.substr(0, colon);
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);
    return {host, std::stoi(s.substr(colon + 1))};
}

AdminServer::AdminServer(const std::string& listen, Handler handler) : handler_(std::move(handler)) {
    auto [host, port] = split_host_port(listen);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host.empty() ? nullptr : host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res)
        throw std::runtime_error("admin: bad listen address " + listen);
    fd_ = ::socket(res->ai_family, SOCK_STREAM | SOCK_CLOEXEC, 0);
    int one = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (fd_ < 0 || ::bind(fd_, res->ai_addr, res->ai_addrlen) != 0 || ::listen(fd_, 64) != 0) {
        ::freeaddrinfo(res);
        throw std::runtime_error("admin: cannot listen on " + listen);
    }
    ::freeaddrinfo(res);
    sockaddr_storage ss{};
    socklen_t len = sizeof(ss);
    ::getsockname(fd_, reinterpret_cast<sockaddr*>(&ss), &len);
    port_ = ss.ss_family == AF_INET6 ? ntohs(reinterpret_cast<sockaddr_in6*>(&ss)->sin6_port)
                                     : ntohs(reinterpret_cast<sockaddr_in*>(&ss)->sin_port);
}

AdminServer::~AdminServer() {
    if (fd_ >= 0) ::close(fd_);
}

void AdminServer::run(std::stop_token stop) {
    while (!stop.stop_requested()) {
        pollfd p{fd_, POLLIN, 0};
        if (::poll(&p, 1, 250) <= 0) continue;
        const int c = ::accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (c < 0) continue;
        timeval tv{2, 0};
        ::setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        serve(c);
        ::close(c);
    }
}

void AdminServer::serve(int fd) {
    std::string req;
    char buf[1024];
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 8192) {
        const auto n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return;
        req.append(buf, static_cast<std::size_t>(n));
    }
    HttpResponse resp;
    if (req.rfind("GET ", 0) != 0) {
        resp = {405, "text/plain", "method not allowed\n"};
    } else {
        const auto end = req.find(' ', 4);
        std::string_view path(req.data() + 4, (end == std::string::npos ? req.size() : end) - 4);
        if (auto q = path.find('?'); q != std::string_view::npos) path = path.substr(0, q);
        resp = handler_(path);
    }
    std::string out = "HTTP/1.0 " + std::to_string(resp.status) + (resp.status == 200 ? " OK" : " ERR") +
                      "\r\nContent-Type: " + resp.content_type + "\r\nContent-Length: " +
                      std::to_string(resp.body.size()) + "\r\nConnection: close\r\n\r\n" + resp.body;
    std::size_t off = 0;
    while (off < out.size()) {
        const auto n = ::send(fd, out.data() + off, out.size() - off, MSG_NOSIGNAL);
        if (n <= 0) return;
        off += static_cast<std::size_t>(n);
    }
}

}  // namespace sb::ops
