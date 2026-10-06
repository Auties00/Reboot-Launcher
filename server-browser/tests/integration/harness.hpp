#pragma once

// Process-level test harness: spawns real sb-edge binaries and talks to them over QUIC.

#include <chrono>
#include <cstdio>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "client/client.hpp"
#include "client/view_mirror.hpp"

namespace sb::test {

using namespace std::chrono_literals;

[[nodiscard]] u16 free_udp_port();
[[nodiscard]] u16 free_tcp_port();
[[nodiscard]] std::optional<std::string> http_get(u16 port, const std::string& path, int* status = nullptr);

struct EdgeOptions {
    u64 edge_id = 1;
    std::string backbone = "inproc";
    std::vector<std::string> nats;
    std::string extra_toml;  // appended verbatim
    u32 shards = 2;
};

class EdgeProcess {
public:
    explicit EdgeProcess(EdgeOptions opts);
    ~EdgeProcess();
    EdgeProcess(const EdgeProcess&) = delete;
    EdgeProcess& operator=(const EdgeProcess&) = delete;

    [[nodiscard]] u16 port() const noexcept { return port_; }
    [[nodiscard]] u16 admin_port() const noexcept { return admin_port_; }
    [[nodiscard]] std::string metrics() const;
    [[nodiscard]] u64 metric(const std::string& name) const;  // sum over labels
    void terminate();  // SIGTERM: graceful drain
    void kill();       // SIGKILL: crash
    bool wait_exit(std::chrono::milliseconds timeout);

private:
    int pid_ = -1;
    u16 port_ = 0;
    u16 admin_port_ = 0;
    std::string config_path_;
    std::string log_path_;
};

// Client wrapper that queues events for the test thread.
class TestClient {
public:
    TestClient(client::ClientRuntime& rt, u16 port, wire::Role role, u64 features = wire::feature::datagrams | wire::feature::zstd);
    ~TestClient();

    client::Client& c() { return *client_; }

    // Waits for an event of type T satisfying pred, skipping others (they stay observable via
    // the mirrors). Returns nullopt on timeout.
    template <class T>
    std::optional<T> wait(std::function<bool(const T&)> pred = {}, std::chrono::milliseconds timeout = 5s) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::unique_lock lk(mu_);
        for (;;) {
            for (auto it = events_.begin(); it != events_.end(); ++it) {
                if (auto* v = std::get_if<T>(&*it); v && (!pred || pred(*v))) {
                    T out = std::move(*v);
                    events_.erase(it);
                    return out;
                }
            }
            if (cv_.wait_until(lk, deadline) == std::cv_status::timeout) {
                for (const auto& e : events_)
                    if (auto* err = std::get_if<wire::Error>(&e))
                        std::fprintf(stderr, "pending error %u for req %u: %s\n", static_cast<unsigned>(err->code), err->req_id,
                                     err->message.c_str());
                return std::nullopt;
            }
        }
    }

    // Waits until the mirror of `view_id` satisfies pred.
    bool wait_mirror(u32 view_id, const std::function<bool(const client::ViewMirror&)>& pred, std::chrono::milliseconds timeout = 5s);

    // Timestamped log of every patch received (receive time in ns, patch).
    std::vector<std::pair<u64, wire::Patch>> patches();

private:
    std::unique_ptr<client::Client> client_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<client::Event> events_;
    std::vector<std::pair<u32, client::ViewMirror>> mirrors_;
    std::vector<std::pair<u64, wire::Patch>> patch_log_;
};

[[nodiscard]] wire::HostRegister host_msg(u32 n, std::string name = {});

}  // namespace sb::test
