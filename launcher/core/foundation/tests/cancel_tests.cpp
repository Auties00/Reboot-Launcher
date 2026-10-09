#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/cancel.hpp"

using namespace rb;

TEST_CASE("A default token is never cancelled", "[foundation][cancel]") {
    const CancelToken token;
    CHECK_FALSE(token.cancelled());
    CHECK_FALSE(token.reason());
    bool ran = false;
    const CancelRegistration registration = token.on_cancel([&](CancelReason) { ran = true; });
    CHECK_FALSE(ran);
}

TEST_CASE("Only the first cancel wins and its reason sticks", "[foundation][cancel]") {
    CancelSource source;
    const CancelToken token = source.token();
    std::vector<CancelReason> seen;
    const CancelRegistration registration = token.on_cancel([&](CancelReason reason) { seen.push_back(reason); });

    CHECK(source.cancel(CancelReason::Deadline));
    CHECK_FALSE(source.cancel(CancelReason::User));
    CHECK(source.cancelled());
    CHECK(token.cancelled());
    CHECK(token.reason() == CancelReason::Deadline);
    REQUIRE(seen.size() == 1);
    CHECK(seen[0] == CancelReason::Deadline);
}

TEST_CASE("A callback registered after the cancel runs at once", "[foundation][cancel]") {
    CancelSource source;
    source.cancel(CancelReason::Shutdown);
    std::optional<CancelReason> seen;
    const CancelRegistration registration = source.token().on_cancel([&](CancelReason reason) { seen = reason; });
    CHECK(seen == CancelReason::Shutdown);
}

TEST_CASE("A reset or destroyed registration never runs", "[foundation][cancel]") {
    CancelSource source;
    int ran = 0;
    CancelRegistration kept = source.token().on_cancel([&](CancelReason) { ++ran; });
    {
        const CancelRegistration dropped = source.token().on_cancel([&](CancelReason) { ran += 10; });
    }
    CancelRegistration reset = source.token().on_cancel([&](CancelReason) { ran += 100; });
    reset.reset();
    CancelRegistration moved = std::move(kept);
    source.cancel(CancelReason::User);
    CHECK(ran == 1);
}

TEST_CASE("The reason is visible to every callback before any runs", "[foundation][cancel]") {
    CancelSource source;
    const CancelToken token = source.token();
    std::vector<std::optional<CancelReason>> observed;
    std::vector<CancelRegistration> registrations;
    for (int i = 0; i < 3; ++i)
        registrations.push_back(token.on_cancel([&](CancelReason) { observed.push_back(token.reason()); }));
    source.cancel(CancelReason::Disconnect);
    REQUIRE(observed.size() == 3);
    for (const auto& reason : observed) CHECK(reason == CancelReason::Disconnect);
}

TEST_CASE("A registration dropped by an earlier callback does not run", "[foundation][cancel]") {
    CancelSource source;
    std::optional<CancelRegistration> other;
    int ran = 0;
    const CancelRegistration first = source.token().on_cancel([&](CancelReason) {
        ++ran;
        other.reset();
    });
    other = source.token().on_cancel([&](CancelReason) { ran += 10; });
    source.cancel(CancelReason::User);
    CHECK(ran == 1);
}

TEST_CASE("A callback may destroy the source that cancels it", "[foundation][cancel]") {
    auto source = std::make_unique<CancelSource>();
    const CancelToken token = source->token();
    int ran = 0;
    const CancelRegistration first = token.on_cancel([&](CancelReason) {
        ++ran;
        source.reset();
    });
    const CancelRegistration second = token.on_cancel([&](CancelReason) { ++ran; });
    CHECK(source->cancel(CancelReason::Shutdown));
    CHECK(ran == 2);
    CHECK(token.reason() == CancelReason::Shutdown);
}

TEST_CASE("Resetting a registration waits for its callback running on another thread",
          "[foundation][cancel][race]") {
    CancelSource source;
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    std::atomic<bool> reset_returned{false};
    std::atomic<bool> returned_while_running{false};
    CancelRegistration registration = source.token().on_cancel([&](CancelReason) {
        entered = true;
        while (!release) std::this_thread::yield();
        returned_while_running = reset_returned.load();
    });

    std::thread canceller([&] { source.cancel(CancelReason::User); });
    while (!entered) std::this_thread::yield();
    std::thread resetter([&] {
        registration.reset();
        reset_returned = true;
    });
    // Gives the resetter time to reach the wait; a reset that does not wait returns meanwhile.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    release = true;
    resetter.join();
    canceller.join();
    CHECK(reset_returned.load());
    CHECK_FALSE(returned_while_running.load());
}

TEST_CASE("A callback may reset its own registration", "[foundation][cancel]") {
    CancelSource source;
    std::optional<CancelRegistration> self;
    int ran = 0;
    self = source.token().on_cancel([&](CancelReason) {
        ++ran;
        self.reset();
    });
    source.cancel(CancelReason::User);
    CHECK(ran == 1);
    CHECK_FALSE(self.has_value());
}

TEST_CASE("Racing cancels from many threads pick exactly one winner", "[foundation][cancel][race]") {
    for (int round = 0; round < 50; ++round) {
        CancelSource source;
        std::atomic<int> callbacks{0};
        const CancelRegistration registration = source.token().on_cancel([&](CancelReason) { ++callbacks; });
        std::atomic<int> winners{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t)
            threads.emplace_back([&, t] {
                if (source.cancel(t % 2 == 0 ? CancelReason::User : CancelReason::Deadline)) ++winners;
            });
        for (std::thread& thread : threads) thread.join();
        CHECK(winners.load() == 1);
        CHECK(callbacks.load() == 1);
    }
}
