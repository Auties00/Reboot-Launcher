#pragma once

#include <deque>
#include <stop_token>

#include "core/flat_map.hpp"
#include "core/mailbox.hpp"
#include "core/rate_limit.hpp"
#include "registry/replica.hpp"

namespace sb::edge {

struct ProberConfig {
    u32 timeout_ms = 1500;
    u32 attempts = 3;
    double max_per_sec = 500;
};

// Verifies that a host's game port answers UDP from the outside. It only ever targets the
// address the host connected from, at a bounded global rate, so it cannot be used to reflect
// traffic at third parties.
class Prober {
public:
    Prober(registry::Replica& replica, ProberConfig cfg);
    ~Prober();
    Prober(const Prober&) = delete;
    Prober& operator=(const Prober&) = delete;

    // Thread-safe; invoked from the replica thread.
    void request(u32 handle, const IpAddr& addr, u16 port);
    void run(std::stop_token stop);

    // The launcher's game-server ping: any reply proves reachability.
    static constexpr u8 kPayload[25] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x04};

private:
    struct Req : MpscNode {
        u32 handle = 0;
        IpAddr addr;
        u16 port = 0;
    };
    struct Pending {
        u32 handle = 0;
        IpAddr addr;
        u16 port = 0;
        u64 deadline_ms = 0;
        u32 attempts_left = 0;
    };

    void send_probe(const Pending& p);
    void receive();
    void expire(u64 now_ms);

    registry::Replica& replica_;
    ProberConfig cfg_;
    Waker waker_;
    Mailbox inbox_{waker_};
    TokenBucket rate_;
    int fd_ = -1;
    std::deque<Pending> queued_;
    FlatMap<u64, Pending> pending_;  // keyed by hash(addr, port)
};

}  // namespace sb::edge
