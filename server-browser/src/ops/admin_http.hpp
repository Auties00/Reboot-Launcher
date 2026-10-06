#pragma once

#include <functional>
#include <stop_token>
#include <string>
#include <utility>

namespace sb::ops {

struct HttpResponse {
    int status = 200;
    std::string content_type = "text/plain; version=0.0.4";
    std::string body;
};

// Tiny blocking HTTP/1.0 responder for /metrics, /healthz and /readyz. Bind it to a private
// interface; it is not meant to face the internet.
class AdminServer {
public:
    using Handler = std::function<HttpResponse(std::string_view path)>;

    AdminServer(const std::string& listen, Handler handler);
    ~AdminServer();
    AdminServer(const AdminServer&) = delete;
    AdminServer& operator=(const AdminServer&) = delete;

    void run(std::stop_token stop);
    [[nodiscard]] int port() const noexcept { return port_; }

private:
    void serve(int fd);

    Handler handler_;
    int fd_ = -1;
    int port_ = 0;
};

// Splits "host:port" / "[v6]:port".
[[nodiscard]] std::pair<std::string, int> split_host_port(const std::string& s);

}  // namespace sb::ops
