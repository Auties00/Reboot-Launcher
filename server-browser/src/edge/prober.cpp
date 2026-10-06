#include "edge/prober.hpp"

#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>

#include "core/time.hpp"
#include "ops/log.hpp"

namespace sb::edge {

namespace {
u64 key_of(const IpAddr& a, u16 port) { return hash_bytes(a.bytes.data(), 16, port); }
}  // namespace

Prober::Prober(registry::Replica& replica, ProberConfig cfg)
    : replica_(replica), cfg_(cfg), rate_(RateSpec::per_second(cfg.max_per_sec, static_cast<u32>(cfg.max_per_sec))) {
    fd_ = ::socket(AF_INET6, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0) throw std::runtime_error("prober socket failed");
    int off = 0;
    ::setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));  // dual-stack: v4 via mapped addresses
}

Prober::~Prober() {
    while (MpscNode* n = inbox_.pop()) delete static_cast<Req*>(n);
    if (fd_ >= 0) ::close(fd_);
}

void Prober::request(u32 handle, const IpAddr& addr, u16 port) {
    auto* r = new Req();
    r->handle = handle;
    r->addr = addr;
    r->port = port;
    inbox_.post(r);
}

void Prober::send_probe(const Pending& p) {
    sockaddr_in6 sa{};
    sa.sin6_family = AF_INET6;
    sa.sin6_port = htons(p.port);
    std::memcpy(&sa.sin6_addr, p.addr.bytes.data(), 16);
    (void)::sendto(fd_, kPayload, sizeof(kPayload), 0, reinterpret_cast<const sockaddr*>(&sa), sizeof(sa));
}

void Prober::receive() {
    u8 buf[2048];
    for (;;) {
        sockaddr_in6 from{};
        socklen_t len = sizeof(from);
        const auto n = ::recvfrom(fd_, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &len);
        if (n < 0) break;
        IpAddr a;
        std::memcpy(a.bytes.data(), &from.sin6_addr, 16);
        auto it = pending_.find(key_of(a, ntohs(from.sin6_port)));
        if (it == pending_.end()) continue;
        replica_.post(registry::kNoShard, 0, registry::ProbeResultReq{.handle = it->second.handle, .reachable = true});
        pending_.erase(it);
    }
}

void Prober::expire(u64 now_ms) {
    std::vector<u64> done;
    for (auto& [k, p] : pending_) {
        if (p.deadline_ms > now_ms) continue;
        if (p.attempts_left > 1) {
            --p.attempts_left;
            p.deadline_ms = now_ms + cfg_.timeout_ms;
            send_probe(p);
        } else {
            replica_.post(registry::kNoShard, 0, registry::ProbeResultReq{.handle = p.handle, .reachable = false});
            done.push_back(k);
        }
    }
    for (u64 k : done) pending_.erase(k);
}

void Prober::run(std::stop_token stop) {
    std::stop_callback wake_on_stop(stop, [this] { waker_.wake(); });
    while (!stop.stop_requested()) {
        const u64 now = mono_ms();
        while (auto* r = static_cast<Req*>(inbox_.pop())) {
            queued_.push_back(Pending{r->handle, r->addr, r->port, 0, cfg_.attempts});
            delete r;
        }
        while (!queued_.empty() && rate_.take(now)) {
            Pending p = queued_.front();
            queued_.pop_front();
            p.deadline_ms = now + cfg_.timeout_ms;
            const u64 k = key_of(p.addr, p.port);
            if (pending_.contains(k)) continue;  // already probing this endpoint
            send_probe(p);
            pending_.emplace(k, p);
        }
        expire(now);

        waker_.announce_sleep();
        if (!inbox_.empty() || stop.stop_requested()) {
            waker_.cancel_sleep();
            continue;
        }
        pollfd fds[2] = {{fd_, POLLIN, 0}, {waker_.fd(), POLLIN, 0}};
        const int timeout = queued_.empty() ? (pending_.empty() ? 1000 : 50) : 5;
        (void)::poll(fds, 2, timeout);
        waker_.drain();
        if (fds[0].revents & POLLIN) receive();
    }
}

}  // namespace sb::edge
