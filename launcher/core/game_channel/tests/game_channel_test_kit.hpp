#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/contracts/game_client.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/game_channel/control_token.hpp"
#include "reboot/game_channel/game_channel_listener.hpp"
#include "reboot/game_channel/token_registry.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/frame_log.hpp"
#include "reboot/testing/game_control_bootstrap.hpp"
#include "reboot/testing/memory_stream_pair.hpp"

namespace reboot::game_channel::test {

namespace gc = contracts::game_client;

// A game-control peer the test drives byte by byte, for the paths no conforming fake takes.
class RawPeer {
public:
    explicit RawPeer(std::unique_ptr<ports::IByteStream> stream) : state_(std::make_shared<State>()) {
        stream_ = std::move(stream);
        stream_->on_read([state = state_](std::span<const u8> bytes) { (void)state->received.feed(bytes); });
        stream_->on_close([state = state_] { state->closed = true; });
    }

    void preamble(u16 payload_abi = gc::kPayloadAbi) { write(game_control_preamble(payload_abi)); }
    void write(std::span<const u8> bytes) { stream_->write(bytes); }
    template <ContractMessage T>
    void send(const T& message) {
        write(encode_contract_frame(message));
    }
    void disconnect() { stream_->close(); }

    [[nodiscard]] const testing::FrameLog& received() const noexcept { return state_->received; }
    [[nodiscard]] bool closed() const noexcept { return state_->closed; }

private:
    struct State {
        testing::FrameLog received{kGameControlFrameCap};
        bool closed = false;
    };

    std::shared_ptr<State> state_;
    std::unique_ptr<ports::IByteStream> stream_;
};

// The strand beside a real WorkerPool, on manual time: the test thread runs what workers posted.
class TestStrand final : public Executor {
public:
    explicit TestStrand(ManualClock& clock) : clock_(clock) {}

    void post(UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        ready_.push_back(std::move(task));
        wake_.notify_one();
    }
    void post_at(SteadyTime when, UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        timed_.emplace(when, std::move(task));
    }

    // A bound, not a sleep: a missing reply fails the test instead of hanging it.
    template <class Done>
    void run_until(Done&& done) {
        while (!done()) {
            UniqueFunction<void()> task;
            {
                std::unique_lock lock(mutex_);
                REQUIRE(wake_.wait_for(lock, std::chrono::seconds{10}, [this] { return !ready_.empty(); }));
                task = std::move(ready_.front());
                ready_.pop_front();
            }
            task();
        }
    }

    void advance(std::chrono::steady_clock::duration by) {
        clock_.advance(by);
        const std::scoped_lock lock(mutex_);
        while (!timed_.empty() && timed_.begin()->first <= clock_.steady_now()) {
            ready_.push_back(std::move(timed_.begin()->second));
            timed_.erase(timed_.begin());
        }
    }

    // Runs what is ready now, waiting for nothing.
    void drain() {
        for (;;) {
            UniqueFunction<void()> task;
            {
                const std::scoped_lock lock(mutex_);
                if (ready_.empty()) return;
                task = std::move(ready_.front());
                ready_.pop_front();
            }
            task();
        }
    }

    [[nodiscard]] bool idle() {
        const std::scoped_lock lock(mutex_);
        return ready_.empty() && !timed_.empty();
    }

private:
    ManualClock& clock_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<UniqueFunction<void()>> ready_;
    std::multimap<SteadyTime, UniqueFunction<void()>> timed_;
};

// The 32 bytes behind a token, read back the way a peer reads REBOOT_CTL_TOKEN.
[[nodiscard]] inline std::array<u8, 32> token_bytes(const ControlToken& token) {
    ports::EnvBlock env;
    env.vars = {{std::string(gc::kEnvCtl), "tcp://127.0.0.1:1"},
                {std::string(gc::kEnvCtlToken), token.env_value().reveal()},
                {std::string(gc::kEnvSession), "00000000-0000-4000-8000-000000000001"},
                {std::string(gc::kEnvRole), "client"}};
    const auto bootstrap = testing::read_game_control_bootstrap(env);
    REQUIRE(bootstrap);
    return bootstrap->token;
}

[[nodiscard]] inline testing::GameControlBootstrap bootstrap_for(const ControlToken& token, SessionId session) {
    return testing::GameControlBootstrap{Endpoint{IpAddress::v4(0x7F000001), Port{1}}, token_bytes(token), session, "client"};
}

// The engine side on manual time: one ManualExecutor is the strand and the I/O thread.
struct Rig {
    Rig() = default;
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    // The returned end is the peer's; the other one the listener adopts.
    [[nodiscard]] std::unique_ptr<ports::IByteStream> connect() {
        testing::MemoryStreamPair pair = testing::make_memory_stream_pair(strand, {}, {});
        listener.adopt(std::move(pair.a));
        return std::move(pair.b);
    }

    void run() { strand.run_all(); }
    void advance(std::chrono::milliseconds by) { strand.advance(by); }

    [[nodiscard]] SessionId new_session() { return SessionId{uuid_v4(random)}; }

    ManualClock clock;
    ManualExecutor strand{clock};
    TimerService timers{clock, strand};
    testing::FakeRandom random{7};
    Redactor redactor;
    TokenRegistry tokens{random, redactor};
    boost::asio::io_context io;
    GameChannelListener listener{io, strand, timers, tokens};
};

}  // namespace reboot::game_channel::test
