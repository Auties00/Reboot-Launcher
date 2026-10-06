#include "harness.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "core/time.hpp"

extern char** environ;

namespace sb::test {

namespace {

u16 free_port(int type) {
    const int fd = ::socket(AF_INET, type, 0);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
    socklen_t len = sizeof(sa);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&sa), &len);
    ::close(fd);
    return ntohs(sa.sin_port);
}

}  // namespace

u16 free_udp_port() { return free_port(SOCK_DGRAM); }
u16 free_tcp_port() { return free_port(SOCK_STREAM); }

std::optional<std::string> http_get(u16 port, const std::string& path, int* status) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
        ::close(fd);
        return std::nullopt;
    }
    const std::string req = "GET " + path + " HTTP/1.0\r\n\r\n";
    (void)::send(fd, req.data(), req.size(), MSG_NOSIGNAL);
    std::string resp;
    char buf[4096];
    for (ssize_t n; (n = ::recv(fd, buf, sizeof(buf), 0)) > 0;) resp.append(buf, static_cast<std::size_t>(n));
    ::close(fd);
    if (resp.size() < 12) return std::nullopt;
    if (status) *status = std::stoi(resp.substr(9, 3));
    const auto body = resp.find("\r\n\r\n");
    return body == std::string::npos ? std::string() : resp.substr(body + 4);
}

EdgeProcess::EdgeProcess(EdgeOptions o) {
    port_ = free_udp_port();
    admin_port_ = free_tcp_port();
    const std::string tag = std::to_string(::getpid()) + "-" + std::to_string(o.edge_id) + "-" + std::to_string(port_);
    config_path_ = "/tmp/sb-test-" + tag + ".toml";
    log_path_ = "/tmp/sb-test-" + tag + ".log";
    std::ofstream cfg(config_path_);
    cfg << "[edge]\nid = " << o.edge_id << "\nlisten = \"127.0.0.1:" << port_ << "\"\nshards = " << o.shards
        << "\nsearch_workers = 1\ndrain_delay_ms = 0\ndrain_spread_ms = 1000\n"
        << "[admin]\nlisten = \"127.0.0.1:" << admin_port_ << "\"\n"
        << "[log]\nlevel = \"debug\"\njson = false\n"
        << "[tls]\nself_signed = true\n"
        << "[limits]\nconn_per_ip_per_sec = 10000\nconn_per_ip_burst = 10000\nconn_per_subnet_per_sec = 10000\n"
        << "conn_per_subnet_burst = 10000\nquery_per_sec = 1000\nquery_burst = 1000\njoin_per_min = 600\njoin_burst = 100\n"
        << "[registry]\nprobe = false\nmax_hosts_per_ip = 100000\ngrace_ms = 2000\nttl_ms = 3000\nheartbeat_ms = 1000\n"
        << "[backbone]\nkind = \"" << o.backbone << "\"\nreplicas = 1\nlease_ttl_ms = 2000\n";
    if (!o.nats.empty()) {
        cfg << "urls = [";
        for (std::size_t i = 0; i < o.nats.size(); ++i) cfg << (i ? ", " : "") << '"' << o.nats[i] << '"';
        cfg << "]\n";
    }
    cfg << o.extra_toml << '\n';
    cfg.close();

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 2, log_path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    const std::string bin = SB_EDGE_BIN;
    const char* argv[] = {bin.c_str(), "--config", config_path_.c_str(), nullptr};
    if (posix_spawn(&pid_, bin.c_str(), &fa, nullptr, const_cast<char* const*>(argv), environ) != 0)
        throw std::runtime_error("cannot spawn " + bin);
    posix_spawn_file_actions_destroy(&fa);

    const auto deadline = std::chrono::steady_clock::now() + 30s;
    while (std::chrono::steady_clock::now() < deadline) {
        int st = 0;
        if (http_get(admin_port_, "/readyz", &st) && st == 200) return;
        int ws = 0;
        if (::waitpid(pid_, &ws, WNOHANG) == pid_) {
            pid_ = -1;
            std::ifstream f(log_path_);
            throw std::runtime_error("sb-edge exited during startup:\n" + std::string(std::istreambuf_iterator<char>(f), {}));
        }
        std::this_thread::sleep_for(50ms);
    }
    throw std::runtime_error("sb-edge did not become ready; log: " + log_path_);
}

EdgeProcess::~EdgeProcess() {
    if (pid_ > 0) {
        ::kill(pid_, SIGINT);
        if (!wait_exit(10s)) kill();
    }
    std::remove(config_path_.c_str());
}

std::string EdgeProcess::metrics() const { return http_get(admin_port_, "/metrics").value_or(""); }

u64 EdgeProcess::metric(const std::string& name) const {
    std::istringstream in(metrics());
    std::string line;
    u64 sum = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.compare(0, name.size(), name) != 0) continue;
        const char next = line.size() > name.size() ? line[name.size()] : ' ';
        if (next != ' ' && next != '{') continue;
        sum += static_cast<u64>(std::stod(line.substr(line.rfind(' ') + 1)));
    }
    return sum;
}

void EdgeProcess::terminate() {
    if (pid_ > 0) ::kill(pid_, SIGTERM);
}

void EdgeProcess::kill() {
    if (pid_ > 0) {
        ::kill(pid_, SIGKILL);
        wait_exit(5s);
    }
}

bool EdgeProcess::wait_exit(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (pid_ > 0 && std::chrono::steady_clock::now() < deadline) {
        int ws = 0;
        if (::waitpid(pid_, &ws, WNOHANG) == pid_) {
            pid_ = -1;
            return true;
        }
        std::this_thread::sleep_for(20ms);
    }
    return pid_ <= 0;
}

TestClient::TestClient(client::ClientRuntime& rt, u16 port, wire::Role role, u64 features) {
    client::Client::Options o{.host = "127.0.0.1", .port = port, .role = role, .features = features, .client_version = "sb-test"};
    o.on_event = [this](client::Event& e) {
        std::lock_guard lk(mu_);
        if (auto* s = std::get_if<client::SnapshotEvent>(&e)) {
            bool found = false;
            for (auto& [vid, m] : mirrors_)
                if (vid == s->snapshot.view_id) {
                    m.on_snapshot(s->snapshot);
                    found = true;
                }
            if (!found) {
                mirrors_.emplace_back(s->snapshot.view_id, client::ViewMirror{});
                mirrors_.back().second.on_snapshot(s->snapshot);
            }
        } else if (auto* d = std::get_if<client::DeltaEvent>(&e)) {
            const u64 now = mono_ns();
            for (const auto& p : d->delta.patches) patch_log_.emplace_back(now, p);
            for (auto& [vid, m] : mirrors_)
                if (vid == d->delta.view_id) m.on_delta(d->delta);
        }
        events_.push_back(std::move(e));
        cv_.notify_all();
    };
    client_ = std::make_unique<client::Client>(rt, std::move(o));
    client_->connect();
}

TestClient::~TestClient() {
    client_->close();
    client_.reset();
}

bool TestClient::wait_mirror(u32 view_id, const std::function<bool(const client::ViewMirror&)>& pred,
                             std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock lk(mu_);
    for (;;) {
        for (auto& [vid, m] : mirrors_)
            if (vid == view_id && pred(m)) return true;
        if (cv_.wait_until(lk, deadline) == std::cv_status::timeout) return false;
    }
}

std::vector<std::pair<u64, wire::Patch>> TestClient::patches() {
    std::lock_guard lk(mu_);
    return patch_log_;
}

wire::HostRegister host_msg(u32 n, std::string name) {
    wire::HostRegister r{.name = name.empty() ? "it server " + std::to_string(n) : std::move(name),
                         .description = "integration test",
                         .version = "4.5",
                         .author = "tests",
                         .game_port = 7000 + n,
                         .max_players = 100};
    r.id.bytes[0] = 0x17;
    r.id.bytes[1] = static_cast<u8>(::getpid());
    r.id.bytes[2] = static_cast<u8>(::getpid() >> 8);
    r.id.bytes[15] = static_cast<u8>(n);
    r.id.bytes[14] = static_cast<u8>(n >> 8);
    return r;
}

}  // namespace sb::test
