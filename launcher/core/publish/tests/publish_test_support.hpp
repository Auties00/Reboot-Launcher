#pragma once

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/publish/host_identity_store.hpp"
#include "reboot/publish/publish_notice_sink.hpp"
#include "reboot/testing/fake_quic_peer.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "wire/frame.hpp"
#include "wire/messages.hpp"

namespace rb::publish::test {

namespace wire = sb::wire;

// The strand beside the store's real WorkerPool: workers post from their threads, timed tasks follow
// the ManualClock, and only the test thread runs anything.
class TestStrand final : public Executor {
public:
    explicit TestStrand(ManualClock& clock) : clock_(clock) {}

    void post(UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        ready_.push_back(std::move(task));
        posted_.notify_one();
    }
    void post_at(SteadyTime when, UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        timed_.emplace(when, std::move(task));
    }

    std::size_t run_ready() {
        std::size_t ran = 0;
        while (UniqueFunction<void()> task = next(false)) {
            task();
            ++ran;
        }
        return ran;
    }

    // Runs tasks, waiting for workers to post, until `done` holds.
    template <class Done>
    void run_until(Done&& done) {
        run_ready();
        while (!done()) {
            UniqueFunction<void()> task = next(true);
            REQUIRE(static_cast<bool>(task));
            task();
        }
    }

    // Moves the clock from one due time to the next, so each timed task runs at its own deadline.
    void advance(std::chrono::steady_clock::duration by) {
        const SteadyTime target = clock_.steady_now() + by;
        run_ready();
        for (;;) {
            std::optional<SteadyTime> due;
            {
                const std::scoped_lock lock(mutex_);
                if (!timed_.empty() && timed_.begin()->first <= target) due = timed_.begin()->first;
            }
            if (!due) break;
            if (*due > clock_.steady_now()) clock_.advance(*due - clock_.steady_now());
            run_ready();
        }
        if (target > clock_.steady_now()) clock_.advance(target - clock_.steady_now());
        run_ready();
    }

private:
    UniqueFunction<void()> next(bool wait) {
        std::unique_lock lock(mutex_);
        const SteadyTime now = clock_.steady_now();
        while (!timed_.empty() && timed_.begin()->first <= now) {
            ready_.push_back(std::move(timed_.begin()->second));
            timed_.erase(timed_.begin());
        }
        // A bound, not a sleep: a missing reply fails the test instead of hanging it.
        if (wait && !posted_.wait_for(lock, std::chrono::seconds{10}, [this] { return !ready_.empty(); })) return {};
        if (ready_.empty()) return {};
        UniqueFunction<void()> task = std::move(ready_.front());
        ready_.pop_front();
        return task;
    }

    ManualClock& clock_;
    std::mutex mutex_;
    std::condition_variable posted_;
    std::deque<UniqueFunction<void()>> ready_;
    std::multimap<SteadyTime, UniqueFunction<void()>> timed_;
};

inline const NativePath kIdentityDir = NativePath("/data/state/host-identity");

[[nodiscard]] inline HostProfileId profile_id(u8 seed) {
    HostProfileId id;
    id.value.bytes.fill(seed);
    id.value.bytes[6] = 0x40;
    return id;
}

[[nodiscard]] inline SessionId session_id(u8 seed) {
    SessionId id;
    id.value.bytes.fill(seed);
    id.value.bytes[6] = 0x41;
    return id;
}

[[nodiscard]] inline std::array<u8, kHostTokenSize> token_bytes(u8 seed) {
    std::array<u8, kHostTokenSize> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<u8>(seed + i);
    return bytes;
}

[[nodiscard]] inline NativePath identity_file(const HostProfileId& profile) {
    return kIdentityDir / (format_uuid(profile.value) + ".json");
}

// The engine's runtime around one store, on a strand the test drives.
struct StoreRig {
    StoreRig() : timers(clock, strand), events(EngineEpoch{1}), ops(clock, timers, events) {}

    // Waits for the store's queued disk work by flushing behind it.
    Result<void> settle() {
        std::optional<Result<void>> flushed;
        store.flush([&](Result<void> result) { flushed = std::move(result); });
        strand.run_until([&] { return flushed.has_value(); });
        return *flushed;
    }

    // Runs what the one worker finished before now, imports and exports included.
    void drain_workers() {
        bool drained = false;
        workers.submit<bool>([](CancelToken) -> Result<bool> { return true; }, CancelToken{}, strand,
                             [&drained](Result<bool>) { drained = true; });
        strand.run_until([&] { return drained; });
    }

    [[nodiscard]] std::optional<ErasedOutcome> wait_op(OpId op) {
        strand.run_until([&] { return ops.outcome(op).has_value(); });
        return ops.outcome(op);
    }

    ManualClock clock;
    TestStrand strand{clock};
    TimerService timers;
    EventBus events;
    OpRegistry ops;
    testing::InMemoryFileSystem fs;
    testing::FakeRandom random{11};
    WorkerPool workers{1};
    HostIdentityStore store{fs, workers, strand, random, ops, kIdentityDir};
};

class RecordingNotices final : public IPublishNoticeSink {
public:
    void on_publish_notice(const PublishNotice& notice) override { received.push_back(notice); }

    [[nodiscard]] std::size_t count(PublishNoticeKind kind) const {
        std::size_t n = 0;
        for (const PublishNotice& notice : received)
            if (notice.kind == kind) ++n;
        return n;
    }

    std::vector<PublishNotice> received;
};

// The edge's side of one host connection: decodes what the publisher sent and answers.
class Edge {
public:
    explicit Edge(testing::FakeQuicPeer& peer) : peer_(&peer) {}

    [[nodiscard]] testing::FakeQuicPeer& peer() const { return *peer_; }
    [[nodiscard]] u64 control() const { return peer_->streams().front(); }

    // The next frame the publisher sent on the control stream, which must be a T.
    template <class T>
    T expect() {
        pull();
        REQUIRE_FALSE(queue_.empty());
        auto [type, payload] = std::move(queue_.front());
        queue_.pop_front();
        REQUIRE(type == wire::frame_type_v<T>);
        T message;
        REQUIRE(wire::decode(payload, message));
        return message;
    }

    // Nothing new on the control stream.
    [[nodiscard]] bool idle() {
        pull();
        return queue_.empty();
    }

    template <class T>
    void send(const T& message) {
        peer_->send(control(), wire::frame_bytes(message), false);
    }

    void welcome(u32 heartbeat_ms = 3000, u32 update_per_sec = 0) {
        wire::Welcome welcome;
        welcome.edge_id = 7;
        welcome.features = wire::feature::datagrams;
        welcome.limits.heartbeat_ms = heartbeat_ms;
        welcome.limits.ttl_ms = heartbeat_ms * 3;
        welcome.limits.host_update_burst = 5;
        welcome.limits.host_update_per_sec = update_per_sec;
        send(welcome);
    }

    void registered(u32 req_id, std::optional<std::array<u8, kHostTokenSize>> token = std::nullopt,
                    u32 heartbeat_ms = 3000) {
        wire::HostRegistered answer;
        answer.req_id = req_id;
        answer.token = token;
        answer.handle = 99;
        answer.heartbeat_ms = heartbeat_ms;
        answer.ttl_ms = heartbeat_ms * 3;
        // 203.0.113.7, IPv4-mapped as the edge sends it.
        answer.observed_address = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 203, 0, 113, 7};
        send(answer);
    }

    void error(u32 req_id, wire::ErrorCode code, std::string message = {}, u32 retry_after_ms = 0) {
        send(wire::Error{req_id, code, std::move(message), retry_after_ms});
    }

private:
    void pull() {
        if (peer_->streams().empty()) return;
        const std::vector<u8> all = peer_->received(control());
        const std::span<const u8> fresh = std::span<const u8>(all).subspan(consumed_);
        REQUIRE(wire::for_each_frame(fresh, 1 << 20, [&](const wire::FrameView& frame) {
            queue_.emplace_back(frame.type, std::vector<u8>(frame.payload.begin(), frame.payload.end()));
            return true;
        }));
        consumed_ = all.size();
    }

    testing::FakeQuicPeer* peer_;
    std::size_t consumed_ = 0;
    std::deque<std::pair<wire::FrameType, std::vector<u8>>> queue_;
};

}  // namespace rb::publish::test
