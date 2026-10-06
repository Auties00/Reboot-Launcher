#pragma once

#include <atomic>
#include <stop_token>
#include <string>
#include <vector>

#include "core/mailbox.hpp"
#include "registry/events.hpp"
#include "registry/replica.hpp"
#include "registry/search.hpp"

namespace sb::registry {

struct TextQueryReq : MpscNode {
    u16 shard = kNoShard;
    u64 conn = 0;
    u32 req_id = 0;
    wire::ViewSpec spec;
    std::string text;
    u32 limit = 0;
    wire::Bytes cursor;
};

// Owns a private search index fed by the ring and answers text queries off the replica thread.
class SearchWorker {
public:
    SearchWorker(Replica& replica, std::vector<Mailbox*> shard_mailboxes, u32 max_limit);
    SearchWorker(const SearchWorker&) = delete;
    SearchWorker& operator=(const SearchWorker&) = delete;
    ~SearchWorker();

    void post(TextQueryReq* q) noexcept { inbox_.post(q); }
    void run(std::stop_token stop);

    [[nodiscard]] u64 queries() const noexcept { return queries_.load(std::memory_order_relaxed); }

private:
    bool consume_ring();
    bool answer_queries();

    Replica& replica_;
    std::vector<Mailbox*> shards_;
    u32 max_limit_;
    Waker waker_;
    Mailbox inbox_{waker_};
    BroadcastRing<RingEvent*>::Consumer& consumer_;
    SearchIndex index_;
    std::atomic<u64> queries_{0};
};

}  // namespace sb::registry
