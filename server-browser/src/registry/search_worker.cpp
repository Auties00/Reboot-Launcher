#include "registry/search_worker.hpp"

#include <chrono>

namespace sb::registry {

SearchWorker::SearchWorker(Replica& replica, std::vector<Mailbox*> shard_mailboxes, u32 max_limit)
    : replica_(replica),
      shards_(std::move(shard_mailboxes)),
      max_limit_(max_limit),
      consumer_(replica.ring().add_consumer(&waker_)) {}

SearchWorker::~SearchWorker() {
    while (MpscNode* n = inbox_.pop()) delete static_cast<TextQueryReq*>(n);
}

bool SearchWorker::consume_ring() {
    auto& ring = replica_.ring();
    const u64 avail = ring.available();
    u64 seq = consumer_.cursor.load(std::memory_order_relaxed);
    if (seq == avail) return false;
    for (; seq < avail; ++seq) {
        const RingEvent* ev = ring.at(seq);
        if (ev->kind != RingEvent::Kind::mutation || ev->handle == kNoHandle) continue;
        if (ev->pub) index_.upsert(*ev->pub);
        else index_.remove(ev->handle);
    }
    BroadcastRing<RingEvent*>::advance(consumer_, avail);
    return true;
}

bool SearchWorker::answer_queries() {
    bool did = false;
    for (int budget = 64; budget > 0; --budget) {
        auto* q = static_cast<TextQueryReq*>(inbox_.pop());
        if (!q) break;
        did = true;
        bool bad_cursor = false;
        const u32 limit = q->limit == 0 ? 50 : std::min(q->limit, max_limit_);
        wire::QueryResult res = index_.query(q->spec, q->text, limit, q->cursor, bad_cursor);
        res.req_id = q->req_id;
        ReplyPayload reply;
        if (bad_cursor) reply = wire::Error{.req_id = q->req_id, .code = wire::ErrorCode::bad_request, .message = "invalid cursor"};
        else reply = std::move(res);
        if (q->shard < shards_.size()) shards_[q->shard]->post(new ShardMsg(q->conn, std::move(reply)));
        queries_.fetch_add(1, std::memory_order_relaxed);
        delete q;
        // Stay current between queries so results never lag far behind the ring.
        consume_ring();
    }
    return did;
}

void SearchWorker::run(std::stop_token stop) {
    std::stop_callback wake_on_stop(stop, [this] { waker_.wake(); });
    while (!stop.stop_requested()) {
        bool did = consume_ring();
        did |= answer_queries();
        if (did) continue;
        waker_.announce_sleep();
        if (replica_.ring().available() != consumer_.cursor.load(std::memory_order_relaxed) || !inbox_.empty() ||
            stop.stop_requested()) {
            waker_.cancel_sleep();
            continue;
        }
        waker_.block(std::chrono::milliseconds(1000));
    }
    consumer_.active.store(false, std::memory_order_release);
}

}  // namespace sb::registry
