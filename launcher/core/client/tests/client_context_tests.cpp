#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "api_event_kind.hpp"
#include "api_event_payload.hpp"
#include "api_outcome.hpp"
#include "call_failure.hpp"
#include "client_test_kit.hpp"
#include "messages.hpp"
#include "reboot/ipc/ipc_limits.hpp"
#include "wire/codec.hpp"

using namespace reboot;
using namespace reboot::client;
using namespace reboot::client::test;
using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""s;

namespace {

using CallOutcome = std::optional<CallResult<std::vector<u8>>>;

CallOutcome& call(ContextFixture& f, CallOutcome& out, u32 method, std::string_view request,
                  std::chrono::milliseconds timeout = 0ms) {
    f.context->call(method, bytes_of(request), timeout, [&out](CallResult<std::vector<u8>> result) {
        REQUIRE_FALSE(out);
        out = std::move(result);
    });
    f.executor.run_all();
    return out;
}

// Starts kMethod and has the engine answer Started{op_id}.
u64 start_op(ContextFixture& f, u64 op_id) {
    std::optional<CallResult<u64>> started;
    f.context->start(kMethod, bytes_of("go"), std::nullopt, [&started](CallResult<u64> result) { started = std::move(result); });
    f.executor.run_all();
    f.engine.send(wire::Started{f.engine.last<wire::Start>().req_id, op_id});
    f.executor.run_all();
    REQUIRE(started);
    REQUIRE(*started);
    return **started;
}

[[nodiscard]] std::optional<wire::WireEvent> next(ContextFixture& f, u64 sub) {
    auto event = f.context->next_event(sub, 0ms);
    REQUIRE(event);
    f.executor.run_all();
    return std::move(*event);
}

[[nodiscard]] std::vector<u32> drain_kinds(ContextFixture& f, u64 sub) {
    std::vector<u32> kinds;
    while (auto event = next(f, sub)) kinds.push_back(event->kind);
    return kinds;
}

[[nodiscard]] u32 kind(ApiEventKind value) { return static_cast<u32>(value); }

[[nodiscard]] std::vector<u8> outcome_of(const wire::WireEvent& event) {
    ApiEventPayload payload;
    REQUIRE(sb::wire::decode(event.payload, payload));
    REQUIRE(payload.op_completed);
    return *payload.op_completed;
}

std::atomic<int> g_wakes{0};
void count_wake(std::uintptr_t user) {
    CHECK(user == 42);
    ++g_wakes;
}

}  // namespace

TEST_CASE("connect sends the caller's Hello and lets the engine take the foreground", "[client][context]") {
    ContextFixture f;
    f.caller.set(ports::CallerContext{"session-9", true, true, {{"DISPLAY", ":0"}}});
    auto connected = f.connect();
    REQUIRE(connected);
    CHECK_FALSE(connected->image_warning);

    const auto hello = f.engine.last<wire::Hello>();
    CHECK(hello.client_kind == wire::ClientKind::Test);
    CHECK(hello.client_build == REBOOT_BUILD_ID);
    CHECK(hello.abi_version == (u32{RB_ABI_MAJOR} << 16 | RB_ABI_MINOR));
    CHECK(hello.pid == kSelf.pid);
    CHECK(hello.caller_context.os_session == "session-9");
    CHECK(hello.caller_context.elevated);
    REQUIRE(hello.caller_context.display_env.size() == 1);
    CHECK(hello.caller_context.display_env[0].name == "DISPLAY");
    CHECK(hello.caller_context.display_env[0].value == ":0");
    CHECK(f.caller.foreground_allowed() == std::vector<u32>{kEnginePid});
}

TEST_CASE("an engine running from another image connects with a warning", "[client][context]") {
    wire::HelloAck ack = hello_ack();
    ack.image_path = to_wire(testing::default_fake_root() / "elsewhere" / "reboot-engine");
    ContextFixture f(ipc::LaunchMode::Autostart, ack);
    auto connected = f.connect();
    REQUIRE(connected);
    REQUIRE(connected->image_warning);
    CHECK(connected->image_warning->is(ipc::kEngineImageDiffers));
}

TEST_CASE("ConnectOnly with no engine fails within the deadline and starts nothing", "[client][context]") {
    ContextFixture f(ipc::LaunchMode::ConnectOnly);
    auto created = ClientContext::create(f.deps(), settings(ipc::LaunchMode::ConnectOnly, 2s));
    REQUIRE(created);
    std::optional<Result<Connected>> result;
    (*created)->connect([&result](Result<Connected> connected) { result = std::move(connected); });
    f.executor.advance(2s);
    REQUIRE(result);
    REQUIRE_FALSE(*result);
    CHECK(result->error().is(ipc::kEngineUnavailable));
    CHECK(status_for(local_failure(result->error())) == RB_E_ENGINE_UNAVAILABLE);
    CHECK(f.starter.calls() == 0);
}

TEST_CASE("the data root falls back to REBOOT_LAUNCHER_HOME, then the platform default", "[client][context]") {
    ContextFixture f;
    f.engine.listen();
    ConnectSettings unset = settings(ipc::LaunchMode::ConnectOnly, 2s);
    unset.data_root.reset();
    for (const std::optional<std::string>& home : {std::optional<std::string>{}, std::optional<std::string>{
                                                                                     display_utf8(data_root())}}) {
        ClientDeps deps = f.deps();
        deps.launcher_home = home;
        auto created = ClientContext::create(deps, unset);
        REQUIRE(created);
        std::optional<Result<Connected>> result;
        (*created)->connect([&result](Result<Connected> connected) { result = std::move(connected); });
        f.executor.run_all();
        REQUIRE(result);
        CHECK(result->has_value());
    }
}

TEST_CASE("a user id the endpoint cannot be named after is refused", "[client][context]") {
    ContextFixture f;
    ClientDeps deps = f.deps();
    deps.self.user_id = "../other";
    auto created = ClientContext::create(deps, settings(ipc::LaunchMode::ConnectOnly, 2s));
    REQUIRE_FALSE(created);
    CHECK(created.error().is(ipc::kInvalidEndpointInput));
}

TEST_CASE("a call carries its timeout and returns the engine's payload or Diagnostic", "[client][context]") {
    ContextFixture f;
    f.connected();
    CallOutcome first;
    call(f, first, kMethod, "ping", 5000ms);
    const auto sent = f.engine.last<wire::Call>();
    CHECK(sent.method_id == kMethod);
    CHECK(sent.payload == bytes_of("ping"));
    CHECK(sent.timeout_ms == 5000);
    f.engine.send(wire::Reply{sent.req_id, bytes_of("pong"), std::nullopt});
    f.executor.run_all();
    REQUIRE(first);
    REQUIRE(*first);
    CHECK(**first == bytes_of("pong"));

    CallOutcome second;
    call(f, second, kMethod, "bad");
    wire::Reply refusal{f.engine.last<wire::Call>().req_id, std::nullopt, std::nullopt};
    refusal.error = contracts::common::WireDiagnostic{"settings.invalid_patch"};
    f.engine.send(refusal);
    f.executor.run_all();
    REQUIRE(second);
    REQUIRE_FALSE(*second);
    CHECK(second->error().remote);
    CHECK(second->error().diagnostic.id == "settings.invalid_patch");
    CHECK(status_for(second->error()) == RB_E_REMOTE);
}

TEST_CASE("an unanswered call times out once and drops the late Reply", "[client][context]") {
    ContextFixture f;
    f.connected();
    int answers = 0;
    std::optional<CallResult<std::vector<u8>>> result;
    f.context->call(kMethod, bytes_of("slow"), 100ms, [&](CallResult<std::vector<u8>> answer) {
        ++answers;
        result = std::move(answer);
    });
    f.executor.advance(99ms);
    CHECK(answers == 0);
    f.executor.advance(1ms);
    REQUIRE(answers == 1);
    REQUIRE_FALSE(*result);
    CHECK(result->error().diagnostic.id == "client.call_timed_out");
    CHECK(status_for(result->error()) == RB_E_TIMEOUT);

    f.engine.send(wire::Reply{f.engine.last<wire::Call>().req_id, bytes_of("late"), std::nullopt});
    f.executor.run_all();
    CHECK(answers == 1);
}

TEST_CASE("a call without a timeout waits for the reply or a lost link", "[client][context]") {
    ContextFixture f(ipc::LaunchMode::ConnectOnly);
    f.connected();
    CallOutcome result;
    call(f, result, kMethod, "wait");
    f.executor.advance(std::chrono::minutes{30});
    CHECK_FALSE(result);
    f.sever();
    REQUIRE(result);
    REQUIRE_FALSE(*result);
    CHECK(result->error().diagnostic.id == "ipc.connection_lost");
    CHECK(status_for(result->error()) == RB_E_CONNECTION_LOST);
}

TEST_CASE("a Started answering a Call is a protocol error", "[client][context]") {
    ContextFixture f;
    f.connected();
    CallOutcome result;
    call(f, result, kMethod, "x");
    f.engine.send(wire::Started{f.engine.last<wire::Call>().req_id, 3});
    f.executor.run_all();
    REQUIRE(result);
    REQUIRE_FALSE(*result);
    CHECK(result->error().diagnostic.id == "ipc.protocol_error");
    CHECK_FALSE(result->error().remote);
}

TEST_CASE("a BootstrapOnly engine is asked only the bootstrap subset", "[client][context]") {
    ContextFixture f(ipc::LaunchMode::Autostart, hello_ack(7, wire::Compatibility::BootstrapOnly));
    f.connected();

    CallOutcome refused;
    call(f, refused, kMethod, "x");
    REQUIRE(refused);
    REQUIRE_FALSE(*refused);
    CHECK(refused->error().diagnostic.id == "ipc.version_mismatch");
    CHECK(status_for(refused->error()) == RB_E_ENGINE_VERSION_MISMATCH);
    CHECK(f.engine.all<wire::Call>().empty());

    CallOutcome status;
    call(f, status, wire::kEngineStatus, "s");
    CHECK(f.engine.last<wire::Call>().method_id == wire::kEngineStatus);
    f.engine.send(wire::Reply{f.engine.last<wire::Call>().req_id, bytes_of("up"), std::nullopt});
    f.executor.run_all();
    REQUIRE(status);
    CHECK(*status);

    std::optional<CallResult<u64>> started;
    f.context->start(kMethod, {}, std::nullopt, [&started](CallResult<u64> result) { started = std::move(result); });
    REQUIRE(started);
    CHECK(started->error().diagnostic.id == "ipc.version_mismatch");

    // The engine would drop these unanswered.
    auto sub = f.context->subscribe({});
    REQUIRE_FALSE(sub);
    CHECK(sub.error().is(ipc::kVersionMismatch));
    auto put = f.context->put_secret(kRevealableTarget, bytes_of("pw"));
    REQUIRE_FALSE(put);
    CHECK(put.error().is(ipc::kVersionMismatch));
    std::optional<CallResult<SecretBytes>> revealed;
    f.context->reveal_secret(kRevealableTarget, [&revealed](CallResult<SecretBytes> result) { revealed = std::move(result); });
    REQUIRE(revealed);
    CHECK(revealed->error().diagnostic.id == "ipc.version_mismatch");
    CHECK(f.engine.all<wire::Subscribe>().empty());
    CHECK(f.engine.all<wire::SecretPut>().empty());
    CHECK(f.engine.all<wire::SecretReveal>().empty());
}

TEST_CASE("start passes the disconnect policy and a refusal creates no op", "[client][context]") {
    ContextFixture f;
    f.connected();
    std::optional<CallResult<u64>> refused;
    f.context->start(kMethod, bytes_of("r"), true, [&refused](CallResult<u64> result) { refused = std::move(result); });
    f.executor.run_all();
    const auto sent = f.engine.last<wire::Start>();
    CHECK(sent.detached == std::optional<bool>{true});
    CHECK(sent.payload == bytes_of("r"));
    wire::Reply refusal{sent.req_id, std::nullopt, contracts::common::WireDiagnostic{"play.wrong_session"}};
    f.engine.send(refusal);
    f.executor.run_all();
    REQUIRE(refused);
    REQUIRE_FALSE(*refused);
    CHECK(start_status_for(refused->error()) == RB_E_ENGINE_OTHER_SESSION);
    CHECK_FALSE(f.context->op_state(1));
}

TEST_CASE("an op's outcome is stored once and reaches a subscription exactly once", "[client][context]") {
    ContextFixture f;
    f.connected();
    const u64 sub = *f.context->subscribe({});
    f.executor.run_all();
    CHECK(f.engine.last<wire::Subscribe>().sub_id == sub);
    const u64 op = start_op(f, 41);
    CHECK(op == 41);
    CHECK(std::holds_alternative<OpPending>(*f.context->op_state(op)));

    // The engine's own OpCompleted is dropped: the outcome supplies it, and its credit goes back.
    wire::WireEvent engine_copy = engine_event(3, kind(ApiEventKind::OpCompleted));
    engine_copy.op = op;
    f.engine.send(wire::EventBatch{sub, {engine_copy}});
    f.engine.send(wire::OpResult{op, bytes_of("outcome")});
    f.engine.send(wire::OpResult{op, bytes_of("replayed")});
    f.executor.run_all();
    CHECK(f.engine.last<wire::Credit>().n == 1);

    const auto stored = f.context->op_state(op);
    REQUIRE(stored);
    REQUIRE(std::holds_alternative<std::vector<u8>>(*stored));
    CHECK(std::get<std::vector<u8>>(*stored) == bytes_of("outcome"));

    auto completed = next(f, sub);
    REQUIRE(completed);
    CHECK(completed->kind == kind(ApiEventKind::OpCompleted));
    CHECK(completed->op == op);
    CHECK(outcome_of(*completed) == bytes_of("outcome"));
    CHECK_FALSE(next(f, sub));

    // Subscribed after the end: nothing to follow.
    const u64 late = *f.context->subscribe({});
    CHECK_FALSE(next(f, late));
}

TEST_CASE("a filter naming a session or another op does not follow an op", "[client][context]") {
    ContextFixture f;
    f.connected();
    // op_id = 9.
    const u64 other_op = *f.context->subscribe(std::vector<u8>{0x18, 0x09});
    const u64 op = start_op(f, 41);
    f.engine.send(wire::OpResult{op, bytes_of("o")});
    f.executor.run_all();
    CHECK_FALSE(next(f, other_op));
}

TEST_CASE("events are credited when taken and a Resync becomes a library event", "[client][context]") {
    ContextFixture f;
    f.connected();
    const u64 sub = *f.context->subscribe({});
    f.engine.send(wire::EventBatch{sub, {engine_event(1), engine_event(2)}});
    f.engine.send(wire::Resync{sub});
    f.executor.run_all();
    CHECK(f.engine.all<wire::Credit>().empty());

    auto first = next(f, sub);
    REQUIRE(first);
    CHECK(first->seq == 1);
    const auto credit = f.engine.last<wire::Credit>();
    CHECK(credit.sub_id == sub);
    CHECK(credit.n == 1);
    CHECK(next(f, sub)->seq == 2);
    auto resync = next(f, sub);
    REQUIRE(resync);
    CHECK(resync->kind == kind(ApiEventKind::Resync));
    CHECK(resync->epoch == 7);
    // A library event earns no credit.
    CHECK(f.engine.all<wire::Credit>().size() == 2);
}

TEST_CASE("subscribe refuses a bad filter and more than kMaxSubscriptions", "[client][context]") {
    ContextFixture f;
    f.connected();
    auto bad = f.context->subscribe(std::vector<u8>{0x0a, 0x05, 0x01});
    REQUIRE_FALSE(bad);
    CHECK(bad.error().is(msg::kInvalidArgument));

    std::vector<u64> subs;
    for (std::size_t i = 0; i < wire::kMaxSubscriptions; ++i) subs.push_back(*f.context->subscribe({}));
    auto over = f.context->subscribe({});
    REQUIRE_FALSE(over);
    CHECK(over.error().is(ipc::kTooManySubscriptions));
    CHECK(status_for(local_failure(over.error())) == RB_E_LIMIT);

    REQUIRE(f.context->unsubscribe(subs.front()));
    f.executor.run_all();
    CHECK(f.engine.last<wire::Unsubscribe>().sub_id == subs.front());
    CHECK(f.context->subscribe({}));
    auto unknown = f.context->unsubscribe(subs.front());
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().is(ipc::kUnknownSubscription));
    CHECK(f.context->next_event(subs.front(), 0ms).error().is(ipc::kUnknownSubscription));
}

TEST_CASE("attach, cancel and release reach the engine for tracked ops only", "[client][context]") {
    ContextFixture f;
    f.connected();
    auto unknown = f.context->cancel(55);
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().is(ipc::kUnknownOp));
    CHECK(status_for(local_failure(unknown.error())) == RB_E_INVALID_ARG);

    REQUIRE(f.context->attach(55));
    REQUIRE(f.context->cancel(55));
    // Cancel is idempotent.
    REQUIRE(f.context->cancel(55));
    f.executor.run_all();
    CHECK(f.engine.last<wire::Attach>().op_id == 55);
    CHECK(f.engine.all<wire::Cancel>().size() == 2);
    CHECK(std::holds_alternative<OpPending>(*f.context->op_state(55)));

    REQUIRE(f.context->release(55));
    f.executor.run_all();
    CHECK(f.engine.last<wire::Release>().op_id == 55);
    CHECK(f.context->op_state(55).error().is(ipc::kUnknownOp));
    CHECK(f.context->release(55).error().is(ipc::kUnknownOp));
}

TEST_CASE("a released op's outcome no longer reaches the subscription", "[client][context]") {
    ContextFixture f;
    f.connected();
    const u64 sub = *f.context->subscribe({});
    const u64 op = start_op(f, 41);
    REQUIRE(f.context->release(op));
    f.engine.send(wire::OpResult{op, bytes_of("o")});
    f.executor.run_all();
    CHECK_FALSE(next(f, sub));
}

TEST_CASE("a wake runs on the executor, once until the queue is drained", "[client][context]") {
    ContextFixture f;
    f.connected();
    g_wakes = 0;
    const u64 sub = *f.context->subscribe({});
    f.engine.send(wire::EventBatch{sub, {engine_event(1)}});
    f.executor.run_all();

    f.context->set_wake(sub, WakeCallback{count_wake, 42});
    CHECK(g_wakes == 0);
    f.executor.run_all();
    CHECK(g_wakes == 1);
    f.engine.send(wire::EventBatch{sub, {engine_event(2)}});
    f.executor.run_all();
    CHECK(g_wakes == 1);

    CHECK(drain_kinds(f, sub).size() == 2);
    f.engine.send(wire::EventBatch{sub, {engine_event(3)}});
    f.executor.run_all();
    CHECK(g_wakes == 2);

    f.context->set_wake(sub, WakeCallback{});
    CHECK(drain_kinds(f, sub).size() == 1);
    f.engine.send(wire::EventBatch{sub, {engine_event(4)}});
    f.executor.run_all();
    CHECK(g_wakes == 2);
    // An unknown subscription is ignored.
    f.context->set_wake(999, WakeCallback{count_wake, 42});
    f.executor.run_all();
    CHECK(g_wakes == 2);
}

TEST_CASE("ForegroundHint lets the named process take the foreground", "[client][context]") {
    ContextFixture f;
    f.connected();
    f.engine.send(wire::ForegroundHint{999});
    f.executor.run_all();
    CHECK(f.caller.foreground_allowed() == std::vector<u32>{kEnginePid, 999});
}

TEST_CASE("a reconnect to the same epoch reattaches ops and resubscribes", "[client][context]") {
    ContextFixture f;
    f.connected();
    const u64 sub = *f.context->subscribe({});
    const u64 op = start_op(f, 41);
    CallOutcome pending;
    call(f, pending, kMethod, "lost");

    f.sever();
    REQUIRE(pending);
    CHECK(pending->error().diagnostic.id == "ipc.connection_lost");
    f.executor.advance(ipc::kReconnectBackoffMin);
    f.executor.run_all();
    CHECK(f.engine.connections == 2);

    CHECK(drain_kinds(f, sub) == std::vector<u32>{kind(ApiEventKind::ConnectionLost), kind(ApiEventKind::Reconnected),
                                                  kind(ApiEventKind::Resync)});
    CHECK(f.engine.last<wire::Attach>().op_id == op);
    CHECK(f.engine.all<wire::Subscribe>().size() == 2);
    CHECK(f.engine.last<wire::Subscribe>().sub_id == sub);
    CHECK(std::holds_alternative<OpPending>(*f.context->op_state(op)));

    // The engine replays the outcome of an op that ended meanwhile.
    f.engine.send(wire::OpResult{op, bytes_of("done")});
    f.executor.run_all();
    auto completed = next(f, sub);
    REQUIRE(completed);
    CHECK(outcome_of(*completed) == bytes_of("done"));
}

TEST_CASE("a reconnect to a new epoch fails the old epoch's ops", "[client][context]") {
    ContextFixture f;
    f.connected();
    const u64 sub = *f.context->subscribe({});
    const u64 op = start_op(f, 41);

    f.engine.ack.epoch = 8;
    f.sever();
    f.executor.advance(ipc::kReconnectBackoffMin);
    f.executor.run_all();

    std::vector<wire::WireEvent> events;
    while (auto event = next(f, sub)) events.push_back(std::move(*event));
    REQUIRE(events.size() == 4);
    CHECK(events[0].kind == kind(ApiEventKind::ConnectionLost));
    CHECK(events[1].kind == kind(ApiEventKind::Reconnected));
    CHECK(events[1].epoch == 8);
    CHECK(events[2].kind == kind(ApiEventKind::OpCompleted));
    CHECK(events[2].op == op);
    CHECK(events[3].kind == kind(ApiEventKind::Resync));
    CHECK(f.engine.all<wire::Attach>().empty());

    const auto state = f.context->op_state(op);
    REQUIRE(std::holds_alternative<std::vector<u8>>(*state));
    ApiOutcome outcome;
    REQUIRE(sb::wire::decode(std::get<std::vector<u8>>(*state), outcome));
    CHECK(outcome.op_id == op);
    CHECK(outcome.method_id == kMethod);
    REQUIRE(outcome.failed);
    CHECK(outcome.failed->id == "ipc.connection_lost");
    CHECK(outcome_of(events[2]) == std::get<std::vector<u8>>(*state));
}

TEST_CASE("a reconnect to another build ends the subscriptions it can no longer feed", "[client][context]") {
    ContextFixture f;
    f.connected();
    const u64 sub = *f.context->subscribe({});
    const u64 op = start_op(f, 41);

    f.engine.ack = hello_ack(8, wire::Compatibility::BootstrapOnly);
    f.sever();
    f.executor.advance(ipc::kReconnectBackoffMin);
    f.executor.run_all();
    REQUIRE(f.engine.connections == 2);

    CHECK(next(f, sub)->kind == kind(ApiEventKind::ConnectionLost));
    CHECK(next(f, sub)->kind == kind(ApiEventKind::Reconnected));
    auto failed = next(f, sub);
    REQUIRE(failed);
    CHECK(failed->kind == kind(ApiEventKind::OpCompleted));
    CHECK(failed->op == op);
    auto closed = f.context->next_event(sub, 0ms);
    REQUIRE_FALSE(closed);
    CHECK(closed.error().is(msg::kClosed));
    CHECK(f.engine.all<wire::Subscribe>().size() == 1);
    CHECK(f.context->subscribe({}).error().is(ipc::kVersionMismatch));
}

TEST_CASE("a failed attach keeps an op that was already attached", "[client][context]") {
    ContextFixture f;
    f.connected();
    const u64 op = start_op(f, 41);
    // The reconnect waits out its backoff, so no link is up.
    f.sever();

    auto again = f.context->attach(op);
    REQUIRE_FALSE(again);
    CHECK(again.error().is(ipc::kConnectionLost));
    CHECK(std::holds_alternative<OpPending>(*f.context->op_state(op)));

    auto fresh = f.context->attach(99);
    REQUIRE_FALSE(fresh);
    CHECK(f.context->op_state(99).error().is(ipc::kUnknownOp));
}

TEST_CASE("cancelling an ended op succeeds without a link", "[client][context]") {
    ContextFixture f(ipc::LaunchMode::ConnectOnly);
    f.connected();
    const u64 op = start_op(f, 41);
    f.engine.send(wire::OpResult{op, bytes_of("done")});
    f.executor.run_all();
    f.sever();

    REQUIRE(f.context->cancel(op));
    CHECK(f.engine.all<wire::Cancel>().empty());
}

TEST_CASE("a ConnectOnly loss is final: queued events drain, then client.closed", "[client][context]") {
    ContextFixture f(ipc::LaunchMode::ConnectOnly);
    f.connected();
    const u64 sub = *f.context->subscribe({});
    const u64 op = start_op(f, 41);
    f.engine.send(wire::EventBatch{sub, {engine_event(1)}});
    f.executor.run_all();
    f.sever();

    CHECK(next(f, sub)->seq == 1);
    CHECK(next(f, sub)->kind == kind(ApiEventKind::OpCompleted));
    CHECK(next(f, sub)->kind == kind(ApiEventKind::ConnectionLost));
    auto closed = f.context->next_event(sub, 0ms);
    REQUIRE_FALSE(closed);
    CHECK(closed.error().is(msg::kClosed));
    CHECK(status_for(local_failure(closed.error())) == RB_E_CLOSED);
    CHECK(std::holds_alternative<std::vector<u8>>(*f.context->op_state(op)));

    CallOutcome after;
    call(f, after, kMethod, "x");
    CHECK(after->error().diagnostic.id == "client.closed");
    CHECK(f.context->subscribe({}).error().is(msg::kClosed));
    CHECK(f.context->attach(7).error().is(msg::kClosed));
}

TEST_CASE("close fails waiting calls and releases a reader blocked without a timeout", "[client][context]") {
    ContextFixture f;
    f.connected();
    const u64 sub = *f.context->subscribe({});
    CallOutcome pending;
    call(f, pending, kMethod, "x");

    // The blocked reader posts nothing, so the ManualExecutor stays on this thread.
    std::optional<Result<std::optional<wire::WireEvent>>> read;
    std::thread reader([&] { read = f.context->next_event(sub, std::nullopt); });
    f.context->close();
    reader.join();

    REQUIRE(pending);
    CHECK(pending->error().diagnostic.id == "client.closed");
    REQUIRE(read);
    REQUIRE_FALSE(*read);
    CHECK(read->error().is(msg::kClosed));
    f.executor.run_all();
    CHECK(f.engine.last<wire::Goodbye>().reason == wire::GoodbyeReason::Normal);
}

TEST_CASE("unsubscribe releases a reader blocked on that subscription", "[client][context]") {
    ContextFixture f;
    f.connected();
    const u64 sub = *f.context->subscribe({});
    std::optional<Result<std::optional<wire::WireEvent>>> read;
    std::thread reader([&] { read = f.context->next_event(sub, std::nullopt); });
    REQUIRE(f.context->unsubscribe(sub));
    reader.join();
    REQUIRE(read);
    REQUIRE_FALSE(*read);
    // Either the reader was blocked and saw the close, or it came after the subscription was gone.
    CHECK((read->error().is(msg::kClosed) || read->error().is(ipc::kUnknownSubscription)));
}

TEST_CASE("secrets travel in their own frames", "[client][context]") {
    ContextFixture f;
    f.connected();
    REQUIRE(f.context->put_secret(kRevealableTarget, bytes_of("hunter2")));
    f.executor.run_all();
    const auto put = f.engine.last<wire::SecretPut>();
    CHECK(put.target == kRevealableTarget);
    CHECK(put.bytes == bytes_of("hunter2"));

    std::optional<CallResult<SecretBytes>> revealed;
    f.context->reveal_secret(kRevealableTarget, [&revealed](CallResult<SecretBytes> result) { revealed = std::move(result); });
    f.executor.run_all();
    f.engine.send(wire::Reply{f.engine.last<wire::SecretReveal>().req_id, bytes_of("pw"), std::nullopt});
    f.executor.run_all();
    REQUIRE(revealed);
    REQUIRE(*revealed);
    CHECK((*revealed)->reveal() == bytes_of("pw"));
}

TEST_CASE("log lines reach the engine as LogWrite", "[client][context]") {
    ContextFixture f;
    f.connected();
    f.context->log_write(LogLevel::Warn, "hello");
    f.executor.run_all();
    const auto log = f.engine.last<wire::LogWrite>();
    CHECK(log.level == LogLevel::Warn);
    CHECK(log.text == "hello");
}
