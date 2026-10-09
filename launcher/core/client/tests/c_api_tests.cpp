#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "abi_boundary.hpp"
#include "api_event_kind.hpp"
#include "api_event_payload.hpp"
#include "client_runtime.hpp"
#include "client_test_kit.hpp"
#include "reboot/client.h"
#include "reboot/testing/fake_client_platform.hpp"
#include "wire/codec.hpp"

using namespace rb;
using namespace rb::client;
using namespace rb::client::test;
using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""s;

namespace {

// An engine on its own thread, reached by a ClientRuntime over FakeClientPlatform, all through the C ABI.
struct AbiFixture {
    explicit AbiFixture(std::string build = REBOOT_BUILD_ID, NativePath image = engine_image())
        : engine(std::move(build), std::move(image)) {}
    ~AbiFixture() { rb_ctx_destroy(ctx); }
    AbiFixture(const AbiFixture&) = delete;
    AbiFixture& operator=(const AbiFixture&) = delete;

    rb_status open(ipc::LaunchMode mode = ipc::LaunchMode::Autostart, std::chrono::milliseconds deadline = 10s) {
        auto runtime = ClientRuntime::create(platform.take());
        REQUIRE(runtime);
        // As rb_ctx_create's own entry does.
        clear_last_error();
        return open_context(std::move(*runtime), settings(mode, deadline), &ctx);
    }

    void opened() {
        REQUIRE(engine.listen());
        REQUIRE(open() == RB_OK);
    }

    LiveEngine engine;
    testing::FakeClientPlatform platform{engine.ipc};
    rb_ctx* ctx = nullptr;
};

// Takes the buffer's bytes and releases it.
[[nodiscard]] std::vector<u8> take(rb_buffer& buffer) {
    std::vector<u8> bytes(buffer.data, buffer.data + buffer.size);
    rb_buffer_release(&buffer);
    return bytes;
}

[[nodiscard]] std::string last_error_id() {
    rb_buffer buffer{};
    REQUIRE(rb_last_error(&buffer) == RB_OK);
    const std::vector<u8> bytes = take(buffer);
    if (bytes.empty()) return {};
    contracts::common::WireDiagnostic diagnostic;
    REQUIRE(sb::wire::decode(bytes, diagnostic));
    return diagnostic.id;
}

[[nodiscard]] wire::WireEvent decode_event(rb_buffer& buffer) {
    wire::WireEvent event;
    REQUIRE(sb::wire::decode(take(buffer), event));
    return event;
}

// The library's OpCompleted for `op`; anything else first fails the test.
[[nodiscard]] std::vector<u8> await_completion(rb_ctx* ctx, u64 sub, u64 op) {
    rb_buffer buffer{};
    REQUIRE(rb_events_next(ctx, sub, 10000, &buffer) == RB_OK);
    const wire::WireEvent event = decode_event(buffer);
    REQUIRE(event.kind == static_cast<u32>(ApiEventKind::OpCompleted));
    REQUIRE(event.op == op);
    ApiEventPayload payload;
    REQUIRE(sb::wire::decode(event.payload, payload));
    REQUIRE(payload.op_completed);
    return *payload.op_completed;
}

// A call round trip, after which the engine has handled every frame sent before it.
void sync(rb_ctx* ctx) {
    rb_buffer response{};
    REQUIRE(rb_call(ctx, kMethod, nullptr, 0, 0, &response) == RB_OK);
    rb_buffer_release(&response);
}

void signal_latch(std::uintptr_t user) { reinterpret_cast<CompletionLatch<std::thread::id>*>(user)->set(std::this_thread::get_id()); }

}  // namespace

TEST_CASE("rb_ctx_create autostarts the engine and calls round-trip", "[client][abi][e2e]") {
    AbiFixture f;
    f.platform.starter().on_started([&f](const NativePath&, const DataRoot&) { static_cast<void>(f.engine.listen()); });
    REQUIRE(f.open() == RB_OK);
    CHECK(f.platform.starter().calls() == 1);
    CHECK(last_error_id().empty());

    const std::string request = "abc";
    rb_buffer response{};
    REQUIRE(rb_call(f.ctx, kMethod, reinterpret_cast<const uint8_t*>(request.data()), request.size(), 0, &response) == RB_OK);
    CHECK(take(response) == bytes_of("re:abc"));
    CHECK(rb_call(f.ctx, kMethod, nullptr, 0, 5000, &response) == RB_OK);
    CHECK(take(response) == bytes_of("re:"));

    REQUIRE(rb_call(f.ctx, kFailingMethod, nullptr, 0, 0, &response) == RB_E_REMOTE);
    contracts::common::WireDiagnostic remote;
    REQUIRE(sb::wire::decode(take(response), remote));
    CHECK(remote.id == "ipc.unknown_op");
    CHECK(last_error_id() == "ipc.unknown_op");

    // Output buffers must arrive empty, and a null input needs a zero size.
    rb_buffer full{};
    REQUIRE(rb_call(f.ctx, kMethod, nullptr, 0, 0, &full) == RB_OK);
    CHECK(rb_call(f.ctx, kMethod, nullptr, 0, 0, &full) == RB_E_INVALID_ARG);
    CHECK(last_error_id() == "client.invalid_argument");
    rb_buffer_release(&full);
    CHECK(rb_call(f.ctx, kMethod, nullptr, 4, 0, &response) == RB_E_INVALID_ARG);
    CHECK(rb_call(nullptr, kMethod, nullptr, 0, 0, &response) == RB_E_INVALID_ARG);
    CHECK(rb_call(f.ctx, kMethod, nullptr, 0, 0, nullptr) == RB_E_INVALID_ARG);
}

TEST_CASE("an engine image other than the bundled one leaves a warning in rb_last_error", "[client][abi][e2e]") {
    AbiFixture f(REBOOT_BUILD_ID, testing::default_fake_root() / "other" / "reboot-engine");
    f.opened();
    CHECK(last_error_id() == "ipc.engine_image_differs");
}

TEST_CASE("ConnectOnly with no engine fails rb_ctx_create and leaves no context", "[client][abi][e2e]") {
    AbiFixture f;
    // The starter goes with the runtime that the failed open destroys, so it is checked through its hook.
    f.platform.starter().on_started([](const NativePath&, const DataRoot&) { FAIL_CHECK("ConnectOnly started an engine"); });
    CHECK(f.open(ipc::LaunchMode::ConnectOnly, 200ms) == RB_E_ENGINE_UNAVAILABLE);
    CHECK(f.ctx == nullptr);
    CHECK(last_error_id() == "ipc.engine_unavailable");
}

TEST_CASE("an op started through rb_start completes exactly once", "[client][abi][e2e]") {
    AbiFixture f;
    f.opened();
    uint64_t sub = 0;
    REQUIRE(rb_subscribe(f.ctx, nullptr, 0, &sub) == RB_OK);

    uint64_t op = 0;
    CHECK(rb_start(f.ctx, kMethod, nullptr, 0, RB_START_DETACHED | RB_START_BOUND, &op) == RB_E_INVALID_ARG);
    CHECK(rb_start(f.ctx, kMethod, nullptr, 0, 0x4, &op) == RB_E_INVALID_ARG);
    CHECK(rb_start(f.ctx, kMethod, nullptr, 0, 0, nullptr) == RB_E_INVALID_ARG);
    REQUIRE(rb_start(f.ctx, kMethod, nullptr, 0, RB_START_BOUND, &op) == RB_OK);
    rb_buffer outcome{};
    CHECK(rb_op_result(f.ctx, op, &outcome) == RB_PENDING);

    f.engine.complete_last_op();
    const std::vector<u8> expected = bytes_of(std::to_string(op) + ":completed");
    CHECK(await_completion(f.ctx, sub, op) == expected);
    for (int i = 0; i < 2; ++i) {
        REQUIRE(rb_op_result(f.ctx, op, &outcome) == RB_OK);
        CHECK(take(outcome) == expected);
    }
    rb_buffer event{};
    CHECK(rb_events_next(f.ctx, sub, 0, &event) == RB_PENDING);

    REQUIRE(rb_op_release(f.ctx, op) == RB_OK);
    CHECK(rb_op_result(f.ctx, op, &outcome) == RB_E_INVALID_ARG);
    CHECK(last_error_id() == "ipc.unknown_op");
    CHECK(rb_op_release(f.ctx, op) == RB_E_INVALID_ARG);
    REQUIRE(rb_unsubscribe(f.ctx, sub) == RB_OK);
    CHECK(rb_unsubscribe(f.ctx, sub) == RB_E_INVALID_ARG);
}

TEST_CASE("rb_op_cancel cancels an attached op, whose outcome still arrives", "[client][abi][e2e]") {
    AbiFixture f;
    f.opened();
    uint64_t sub = 0;
    REQUIRE(rb_subscribe(f.ctx, nullptr, 0, &sub) == RB_OK);
    uint64_t op = 0;
    REQUIRE(rb_start(f.ctx, kMethod, nullptr, 0, 0, &op) == RB_OK);
    REQUIRE(rb_op_cancel(f.ctx, op) == RB_OK);
    REQUIRE(rb_op_cancel(f.ctx, op) == RB_OK);
    CHECK(await_completion(f.ctx, sub, op) == bytes_of(std::to_string(op) + ":cancelled"));
    CHECK(rb_op_cancel(f.ctx, op + 100) == RB_E_INVALID_ARG);
}

TEST_CASE("rb_events_next waits for an engine event or its timeout", "[client][abi][e2e]") {
    AbiFixture f;
    f.opened();
    uint64_t sub = 0;
    REQUIRE(rb_subscribe(f.ctx, nullptr, 0, &sub) == RB_OK);
    rb_buffer event{};
    CHECK(rb_events_next(f.ctx, sub, 20, &event) == RB_PENDING);
    CHECK(event.data == nullptr);

    sync(f.ctx);
    rb_status status = RB_E_INTERNAL;
    std::vector<u8> payload;
    std::thread reader([&] {
        rb_buffer got{};
        status = rb_events_next(f.ctx, sub, RB_WAIT_FOREVER, &got);
        if (status == RB_OK) {
            wire::WireEvent decoded;
            if (sb::wire::decode(take(got), decoded)) payload = decoded.payload;
        }
    });
    f.engine.publish(bytes_of("published"));
    reader.join();
    CHECK(status == RB_OK);
    CHECK(payload == bytes_of("published"));
    CHECK(rb_events_next(f.ctx, sub + 1, 0, &event) == RB_E_INVALID_ARG);
    CHECK(last_error_id() == "ipc.unknown_subscription");
}

TEST_CASE("a wake runs on a library thread when an event arrives", "[client][abi][e2e]") {
    AbiFixture f;
    f.opened();
    uint64_t sub = 0;
    REQUIRE(rb_subscribe(f.ctx, nullptr, 0, &sub) == RB_OK);
    CompletionLatch<std::thread::id> woken;
    rb_events_set_wake(f.ctx, sub, signal_latch, reinterpret_cast<std::uintptr_t>(&woken));
    sync(f.ctx);
    f.engine.publish(bytes_of("e"));
    CHECK(woken.wait() != std::this_thread::get_id());
    rb_events_set_wake(f.ctx, sub, nullptr, 0);
    rb_buffer event{};
    REQUIRE(rb_events_next(f.ctx, sub, 0, &event) == RB_OK);
    CHECK(decode_event(event).payload == bytes_of("e"));
}

TEST_CASE("a reconnect blocked in the engine starter delays no event wait", "[client][abi][e2e]") {
    // Before the fixture, since the starter hook may run until the context is destroyed.
    std::promise<void> entered;
    std::promise<void> release;
    std::atomic<bool> first{true};
    std::shared_future<void> released = release.get_future().share();
    AbiFixture f;
    f.platform.starter().on_started([&entered, &first, released](const NativePath&, const DataRoot&) {
        if (first.exchange(false)) entered.set_value();
        static_cast<void>(released.wait_for(10s));
    });
    f.opened();
    uint64_t sub = 0;
    REQUIRE(rb_subscribe(f.ctx, nullptr, 0, &sub) == RB_OK);
    sync(f.ctx);

    // The engine goes away for good, so the reconnect round asks the starter for a new one.
    f.engine.on_strand([&f] { f.engine.server->stop(wire::GoodbyeReason::Shutdown); });
    REQUIRE(entered.get_future().wait_for(10s) == std::future_status::ready);
    rb_buffer event{};
    REQUIRE(rb_events_next(f.ctx, sub, 0, &event) == RB_OK);
    CHECK(decode_event(event).kind == static_cast<u32>(ApiEventKind::ConnectionLost));

    const auto begun = std::chrono::steady_clock::now();
    CHECK(rb_events_next(f.ctx, sub, 50, &event) == RB_PENDING);
    CHECK(std::chrono::steady_clock::now() - begun < 5s);
    release.set_value();
}

TEST_CASE("secrets go to the engine and only the join password comes back", "[client][abi][e2e]") {
    AbiFixture f;
    f.opened();
    const std::vector<u8> secret = bytes_of("hunter2");
    REQUIRE(rb_secret_put(f.ctx, kRevealableTarget.data(), kRevealableTarget.size(), secret.data(), secret.size()) == RB_OK);
    CHECK(rb_secret_put(f.ctx, nullptr, 1, secret.data(), secret.size()) == RB_E_INVALID_ARG);

    rb_buffer out{};
    REQUIRE(rb_secret_reveal(f.ctx, kRevealableTarget.data(), kRevealableTarget.size(), &out) == RB_OK);
    CHECK(take(out) == bytes_of("join-pw"));
    const std::vector<u8> other{2};
    CHECK(rb_secret_reveal(f.ctx, other.data(), other.size(), &out) == RB_E_REMOTE);
    CHECK(last_error_id() == "ipc.reveal_refused");

    // The reveal ran after the put on the same link, so the engine has stored it.
    std::vector<std::pair<wire::Bytes, std::vector<u8>>> stored;
    f.engine.on_strand([&] { stored = f.engine.api.secrets; });
    REQUIRE(stored.size() == 1);
    CHECK(stored[0].first == kRevealableTarget);
    CHECK(stored[0].second == secret);
}

TEST_CASE("another engine build serves only the bootstrap subset through the C ABI", "[client][abi][e2e]") {
    AbiFixture f("another-build");
    f.opened();
    rb_buffer response{};
    CHECK(rb_call(f.ctx, RB_METHOD_SETTINGS_SNAPSHOT, nullptr, 0, 0, &response) == RB_E_ENGINE_VERSION_MISMATCH);
    CHECK(last_error_id() == "ipc.version_mismatch");
    REQUIRE(rb_call(f.ctx, RB_METHOD_ENGINE_STATUS, nullptr, 0, 0, &response) == RB_OK);
    CHECK(take(response) == bytes_of("re:"));
    uint64_t op = 0;
    CHECK(rb_start(f.ctx, kMethod, nullptr, 0, 0, &op) == RB_E_ENGINE_VERSION_MISMATCH);
    uint64_t sub = 0;
    CHECK(rb_subscribe(f.ctx, nullptr, 0, &sub) == RB_E_ENGINE_VERSION_MISMATCH);
}

TEST_CASE("rb_ctx_create refuses missing arguments before touching the platform", "[client][abi]") {
    rb_ctx* ctx = reinterpret_cast<rb_ctx*>(std::uintptr_t{1});
    CHECK(rb_ctx_create(nullptr, &ctx) == RB_E_INVALID_ARG);
    CHECK(ctx == nullptr);
    CHECK(last_error_id() == "client.invalid_argument");
    rb_ctx_options options{sizeof(rb_ctx_options), nullptr, RB_CLIENT_TEST, RB_LAUNCH_CONNECT_ONLY, 0};
    CHECK(rb_ctx_create(&options, nullptr) == RB_E_INVALID_ARG);
    rb_ctx_destroy(nullptr);
}

TEST_CASE("a platform missing a port is an internal bug", "[client][abi]") {
    ports::ClientPlatform platform;
    auto runtime = ClientRuntime::create(std::move(platform));
    REQUIRE_FALSE(runtime);
    CHECK(runtime.error().id == "internal.bug");
}
