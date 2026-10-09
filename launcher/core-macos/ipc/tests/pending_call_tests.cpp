#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <optional>
#include <stdexcept>
#include <thread>

#include "messages.hpp"
#include "pending_call.hpp"

using reboot::ErrorDomain;
using reboot::Result;
using reboot::os_macos::ipc::PendingCall;

namespace {

// Only ever reached by a call that was already released, so its length never matters.
constexpr std::chrono::milliseconds kGenerous{60000};

[[nodiscard]] Result<void> register_failed() {
    return reboot::make_diag(ErrorDomain::Platform, reboot::os_macos::ipc::kAgentRegisterFailed)
        .arg("label", "dev.projectreboot.launcher.engine")
        .fail();
}

}  // namespace

TEST_CASE("a finished call hands out its result", "[pending_call]") {
    PendingCall call;
    REQUIRE(call.start([] { return register_failed(); }));
    const std::optional<Result<void>> result = call.wait_for(kGenerous);
    REQUIRE(result);
    REQUIRE_FALSE(*result);
    CHECK(result->error().is(reboot::os_macos::ipc::kAgentRegisterFailed));
}

TEST_CASE("a call still running gives nothing, then its result to a later wait", "[pending_call]") {
    std::promise<void> gate;
    const std::shared_future<void> opened = gate.get_future().share();
    std::atomic<int> runs{0};
    PendingCall call;
    REQUIRE(call.start([opened, &runs] {
        opened.wait();
        ++runs;
        return Result<void>{};
    }));

    CHECK_FALSE(call.wait_for(std::chrono::milliseconds{0}));
    CHECK_FALSE(call.wait_for(std::chrono::milliseconds{1}));
    gate.set_value();
    const std::optional<Result<void>> result = call.wait_for(kGenerous);
    REQUIRE(result);
    CHECK(*result);
    CHECK(runs == 1);
}

TEST_CASE("a second start while the first call runs is refused", "[pending_call]") {
    std::promise<void> gate;
    const std::shared_future<void> opened = gate.get_future().share();
    std::atomic<int> runs{0};
    PendingCall call;
    REQUIRE(call.start([opened, &runs] {
        opened.wait();
        ++runs;
        return Result<void>{};
    }));
    const Result<void> again = call.start([&runs] {
        ++runs;
        return Result<void>{};
    });
    REQUIRE_FALSE(again);
    CHECK(again.error().domain == ErrorDomain::Internal);
    gate.set_value();
    REQUIRE(call.wait_for(kGenerous));
    CHECK(runs == 1);
}

TEST_CASE("a throwing call reports internal.bug", "[pending_call]") {
    PendingCall call;
    REQUIRE(call.start([]() -> Result<void> { throw std::runtime_error("register failed"); }));
    const std::optional<Result<void>> result = call.wait_for(kGenerous);
    REQUIRE(result);
    REQUIRE_FALSE(*result);
    CHECK(result->error().domain == ErrorDomain::Internal);
}

TEST_CASE("destroying a pending call waits for the call in flight", "[pending_call]") {
    std::promise<void> gate;
    const std::shared_future<void> opened = gate.get_future().share();
    std::atomic<bool> finished{false};
    std::thread opener;
    {
        PendingCall call;
        REQUIRE(call.start([opened, &finished] {
            opened.wait();
            finished = true;
            return Result<void>{};
        }));
        CHECK_FALSE(call.wait_for(std::chrono::milliseconds{0}));
        opener = std::thread([&gate] { gate.set_value(); });
    }
    CHECK(finished);
    opener.join();
}
