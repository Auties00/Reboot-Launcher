#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>

#include "ipc_test_kit.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/ipc/ipc_limits.hpp"

using namespace reboot;
using namespace reboot::ipc::test;
using std::chrono_literals::operator""s;

namespace {

struct Fixture {
    Fixture() { REQUIRE(engine.server->start(kEndpoint).has_value()); }

    [[nodiscard]] std::unique_ptr<RawClient> open() {
        auto stream = engine.connector->connect(kEndpoint, 1s);
        REQUIRE(stream.has_value());
        auto client = std::make_unique<RawClient>(std::move(*stream));
        engine.executor.run_all();
        return client;
    }

    [[nodiscard]] std::unique_ptr<RawClient> greeted(std::string build = std::string(kBuild)) {
        auto client = open();
        client->send(hello(std::move(build)));
        engine.executor.run_all();
        REQUIRE(client->all<wire::HelloAck>().size() == 1);
        return client;
    }

    void run() { engine.executor.run_all(); }

    EngineHarness engine;
};

[[nodiscard]] std::string text(const wire::Bytes& bytes) { return {bytes.begin(), bytes.end()}; }

}  // namespace

TEST_CASE("Hello is answered with HelloAck and the connection's compatibility", "[ipc][server]") {
    Fixture f;
    auto same = f.greeted();
    auto other = f.greeted("0.9.0+old");

    const wire::HelloAck ack = same->all<wire::HelloAck>().front();
    CHECK(ack.engine_build == kBuild);
    CHECK(ack.epoch == 7);
    CHECK(ack.pid == 4321);
    CHECK(ack.canonical_root == to_wire(data_root()));
    CHECK(ack.image_path == to_wire(engine_exe()));
    CHECK(ack.compatibility == wire::Compatibility::Full);
    CHECK(other->all<wire::HelloAck>().front().compatibility == wire::Compatibility::BootstrapOnly);

    REQUIRE(f.engine.api.connected.size() == 2);
    const ipc::ConnectionInfo& info = f.engine.api.connected.front();
    CHECK(info.client_kind == wire::ClientKind::Test);
    CHECK(info.client_pid == 99);
    CHECK(info.caller.os_session == "session-1");
    CHECK(info.peer.user_id == "1000");
    CHECK(f.engine.server->connection_count() == 2);
}

TEST_CASE("anything before Hello closes the connection with ProtocolError", "[ipc][server]") {
    Fixture f;
    auto client = f.open();
    client->send(wire::Call{1, kMethod, {}, 0});
    f.run();

    REQUIRE(client->all<wire::Goodbye>().size() == 1);
    CHECK(client->all<wire::Goodbye>().front().reason == wire::GoodbyeReason::ProtocolError);
    CHECK(client->closed);
    CHECK(f.engine.api.calls.empty());
    CHECK(f.engine.server->connection_count() == 0);
    CHECK(f.engine.api.disconnected.empty());
}

TEST_CASE("a client that never says Hello is dropped after the deadline", "[ipc][server]") {
    Fixture f;
    auto client = f.open();
    f.engine.executor.advance(ipc::kHelloDeadline - 1s);
    CHECK_FALSE(client->closed);
    f.engine.executor.advance(1s);
    CHECK(client->closed);
    CHECK(f.engine.server->connection_count() == 0);
}

TEST_CASE("a greeted client is not dropped by the Hello deadline", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted();
    f.engine.executor.advance(ipc::kHelloDeadline * 2);
    CHECK_FALSE(client->closed);
}

TEST_CASE("bytes that are not a frame close the connection", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted();
    const wire::Bytes garbage{0x3F, 0x05, 1, 2, 3, 4, 5};
    client->send_raw(garbage);
    f.run();
    CHECK(client->closed);
    CHECK(client->all<wire::Goodbye>().back().reason == wire::GoodbyeReason::ProtocolError);
    CHECK(f.engine.api.disconnected.size() == 1);
}

TEST_CASE("Call is answered with the dispatcher's payload or error", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted();
    client->send(wire::Call{1, kMethod, bytes_of("abc"), 0});
    client->send(wire::Call{2, kFailingMethod, {}, 0});
    f.run();

    const auto replies = client->all<wire::Reply>();
    REQUIRE(replies.size() == 2);
    CHECK(replies[0].req_id == 1);
    REQUIRE(replies[0].payload);
    CHECK(text(*replies[0].payload) == "re:abc");
    CHECK_FALSE(replies[0].error);
    CHECK(replies[1].req_id == 2);
    REQUIRE(replies[1].error);
    CHECK(replies[1].error->id == "ipc.unknown_op");
}

TEST_CASE("another build is refused everything but the bootstrap methods", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted("0.9.0+old");
    client->send(wire::Call{1, kMethod, {}, 0});
    client->send(wire::Call{2, wire::kEngineStatus, {}, 0});
    client->send(wire::Start{3, kMethod, {}, std::nullopt});
    f.run();

    const auto replies = client->all<wire::Reply>();
    REQUIRE(replies.size() == 3);
    REQUIRE(replies[0].error);
    CHECK(replies[0].error->id == "ipc.version_mismatch");
    CHECK(replies[1].payload);
    REQUIRE(replies[2].error);
    CHECK(replies[2].error->id == "ipc.version_mismatch");
    REQUIRE(f.engine.api.calls.size() == 1);
    CHECK(f.engine.api.calls.front().first == wire::kEngineStatus);
    CHECK(f.engine.api.policies.empty());
}

TEST_CASE("Start answers Started and later the op's OpResult", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted();
    client->send(wire::Start{1, kMethod, {}, true});
    client->send(wire::Start{2, kMethod, {}, false});
    client->send(wire::Start{3, kMethod, {}, std::nullopt});
    f.run();

    const auto started = client->all<wire::Started>();
    REQUIRE(started.size() == 3);
    CHECK(started[0].req_id == 1);
    REQUIRE(f.engine.api.policies.size() == 3);
    CHECK(f.engine.api.policies[0] == DisconnectPolicy::Detached);
    CHECK(f.engine.api.policies[1] == DisconnectPolicy::BoundToConnection);
    CHECK_FALSE(f.engine.api.policies[2]);
    CHECK(client->all<wire::OpResult>().empty());

    f.engine.api.started[0]->complete(Completed<void>{});
    f.run();
    const auto results = client->all<wire::OpResult>();
    REQUIRE(results.size() == 1);
    CHECK(results[0].op_id == started[0].op_id);
    CHECK(text(results[0].outcome) == std::to_string(started[0].op_id) + ":completed");
}

TEST_CASE("an op that ends inside start gets exactly one OpResult after Started", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted();
    client->send(wire::Start{1, ipc::test::kInstantMethod, {}, std::nullopt});
    f.run();

    REQUIRE(client->received.size() == 3);
    REQUIRE(std::holds_alternative<wire::Started>(client->received[1]));
    REQUIRE(std::holds_alternative<wire::OpResult>(client->received[2]));
    CHECK(std::get<wire::OpResult>(client->received[2]).op_id == std::get<wire::Started>(client->received[1]).op_id);
}

TEST_CASE("a refused Start is answered with a Reply error", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted();
    client->send(wire::Start{4, kFailingMethod, {}, std::nullopt});
    f.run();
    const auto replies = client->all<wire::Reply>();
    REQUIRE(replies.size() == 1);
    CHECK(replies[0].req_id == 4);
    REQUIRE(replies[0].error);
    CHECK(client->all<wire::Started>().empty());
}

TEST_CASE("Attach replays an ended op and fails an unknown one", "[ipc][server]") {
    Fixture f;
    auto starter = f.greeted();
    auto watcher = f.greeted();
    starter->send(wire::Start{1, kMethod, {}, true});
    f.run();
    const u64 op = starter->all<wire::Started>().front().op_id;

    watcher->send(wire::Attach{op});
    f.run();
    CHECK(watcher->all<wire::OpResult>().empty());

    f.engine.api.started[0]->complete(Completed<void>{});
    f.run();
    CHECK(starter->all<wire::OpResult>().size() == 1);
    REQUIRE(watcher->all<wire::OpResult>().size() == 1);

    auto late = f.greeted();
    late->send(wire::Attach{op});
    late->send(wire::Attach{999});
    f.run();
    const auto results = late->all<wire::OpResult>();
    REQUIRE(results.size() == 2);
    CHECK(text(results[0].outcome) == std::to_string(op) + ":completed");
    CHECK(results[1].op_id == 999);
    CHECK(text(results[1].outcome) == "999:failed:ipc.unknown_op");
}

TEST_CASE("Release stops OpResult delivery to that connection", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted();
    client->send(wire::Start{1, kMethod, {}, true});
    f.run();
    const u64 op = client->all<wire::Started>().front().op_id;
    client->send(wire::Release{op});
    f.run();
    f.engine.api.started[0]->complete(Completed<void>{});
    f.run();
    CHECK(client->all<wire::OpResult>().empty());
}

TEST_CASE("Cancel cancels the op", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted();
    client->send(wire::Start{1, kMethod, {}, true});
    f.run();
    const u64 op = client->all<wire::Started>().front().op_id;
    client->send(wire::Cancel{op});
    client->send(wire::Cancel{12345});
    f.run();
    REQUIRE(client->all<wire::OpResult>().size() == 1);
    CHECK(text(client->all<wire::OpResult>().front().outcome) == std::to_string(op) + ":cancelled");
    CHECK_FALSE(client->closed);
}

TEST_CASE("closing a connection cancels its bound ops and tells the dispatcher", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted();
    client->send(wire::Start{1, kMethod, {}, false});
    client->send(wire::Start{2, kMethod, {}, true});
    f.run();
    const auto started = client->all<wire::Started>();
    client.reset();
    f.run();

    CHECK(f.engine.server->connection_count() == 0);
    REQUIRE(f.engine.api.disconnected.size() == 1);
    CHECK(f.engine.api.disconnected.front() == f.engine.api.connected.front().id);
    CHECK(f.engine.api.started[0]->done());
    CHECK(f.engine.api.started[0]->token().reason() == CancelReason::Disconnect);
    CHECK_FALSE(f.engine.api.started[1]->done());
}

TEST_CASE("a client Goodbye closes the connection", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted();
    client->send(wire::Goodbye{wire::GoodbyeReason::Normal});
    f.run();
    CHECK(client->closed);
    CHECK(f.engine.server->connection_count() == 0);
}

TEST_CASE("Ping is answered with Pong", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted();
    client->send(contracts::common::Ping{77});
    f.run();
    REQUIRE(client->all<contracts::common::Pong>().size() == 1);
    CHECK(client->all<contracts::common::Pong>().front().nonce == 77);
}

TEST_CASE("SecretPut reaches the dispatcher and SecretReveal only returns what it allows", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted();
    const SecretBytes secret{bytes_of("hunter2")};
    const SecretBytes put = ipc::IpcCodec::encode_secret_put(bytes_of("target"), secret);
    client->send_raw(put.reveal());
    client->send(wire::SecretReveal{5, kRevealableTarget});
    client->send(wire::SecretReveal{6, bytes_of("other")});
    f.run();

    REQUIRE(f.engine.api.secrets.size() == 1);
    CHECK(text(f.engine.api.secrets[0].first) == "target");
    CHECK(text(f.engine.api.secrets[0].second) == "hunter2");
    const auto replies = client->all<wire::Reply>();
    REQUIRE(replies.size() == 2);
    REQUIRE(replies[0].payload);
    CHECK(text(*replies[0].payload) == "join-pw");
    REQUIRE(replies[1].error);
    CHECK(replies[1].error->id == "ipc.reveal_refused");
}

TEST_CASE("secrets_available changes send every greeted connection a fresh HelloAck", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted();
    auto silent = f.open();
    f.engine.server->set_secrets_available(true);
    f.engine.server->set_secrets_available(true);
    f.run();
    const auto acks = client->all<wire::HelloAck>();
    REQUIRE(acks.size() == 2);
    CHECK(acks[1].secrets_available);
    CHECK(silent->received.empty());
}

TEST_CASE("send_foreground_hint reaches greeted connections", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted();
    f.engine.server->send_foreground_hint(555);
    f.run();
    REQUIRE(client->all<wire::ForegroundHint>().size() == 1);
    CHECK(client->all<wire::ForegroundHint>().front().pid == 555);
}

TEST_CASE("stop sends Goodbye with the reason and stops accepting", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted();
    f.engine.server->stop(wire::GoodbyeReason::Restarting);
    f.run();
    REQUIRE(client->all<wire::Goodbye>().size() == 1);
    CHECK(client->all<wire::Goodbye>().front().reason == wire::GoodbyeReason::Restarting);
    CHECK(client->closed);
    CHECK(f.engine.server->connection_count() == 0);
    CHECK_FALSE(f.engine.ipc.listening(kEndpoint));
    CHECK(f.engine.api.disconnected.size() == 1);
}

TEST_CASE("subscriptions receive EventBatches within the credit window", "[ipc][server][events]") {
    Fixture f;
    auto client = f.greeted();
    client->send(wire::Subscribe{1, {static_cast<u8>(EventKind::NoticeAdded)}});
    f.run();

    for (u32 i = 0; i < ipc::kEventCreditWindow + 10; ++i)
        f.engine.events.publish(EventKind::NoticeAdded, TestEvent{bytes_of("n" + std::to_string(i))});
    f.engine.events.publish(EventKind::LogLine, TestEvent{bytes_of("filtered")});
    f.run();

    std::size_t delivered = 0;
    for (const auto& batch : client->all<wire::EventBatch>()) {
        CHECK(batch.sub_id == 1);
        CHECK(batch.events.size() <= ipc::kMaxEventsPerBatch);
        delivered += batch.events.size();
    }
    CHECK(delivered == ipc::kEventCreditWindow);
    CHECK(text(client->all<wire::EventBatch>().front().events.front().payload) == "n0");

    client->send(wire::Credit{1, 5});
    f.run();
    delivered = 0;
    for (const auto& batch : client->all<wire::EventBatch>()) delivered += batch.events.size();
    CHECK(delivered == ipc::kEventCreditWindow + 5);

    client->send(wire::Unsubscribe{1});
    client->send(wire::Credit{1, 100});
    f.run();
    delivered = 0;
    for (const auto& batch : client->all<wire::EventBatch>()) delivered += batch.events.size();
    CHECK(delivered == ipc::kEventCreditWindow + 5);
    CHECK_FALSE(client->closed);
}

TEST_CASE("events published in one strand task share one EventBatch", "[ipc][server][events]") {
    Fixture f;
    auto client = f.greeted();
    client->send(wire::Subscribe{1, {}});
    f.run();
    f.engine.events.publish(EventKind::NoticeAdded, TestEvent{bytes_of("a")});
    f.engine.events.publish(EventKind::NoticeAdded, TestEvent{bytes_of("b")});
    f.run();
    REQUIRE(client->all<wire::EventBatch>().size() == 1);
    const auto events = client->all<wire::EventBatch>().front().events;
    REQUIRE(events.size() == 2);
    CHECK(events[0].seq < events[1].seq);
    CHECK(events[0].epoch == 7);
}

TEST_CASE("bad, duplicate and excess Subscribes are dropped and the connection stays up", "[ipc][server][events]") {
    Fixture f;
    auto client = f.greeted();
    client->send(wire::Subscribe{1, kBadFilter});
    for (u64 sub = 10; sub < 10 + contracts::ipc::kMaxSubscriptions + 1; ++sub) client->send(wire::Subscribe{sub, {}});
    client->send(wire::Subscribe{10, {}});
    f.run();
    f.engine.events.publish(EventKind::NoticeAdded, TestEvent{bytes_of("x")});
    f.run();

    CHECK_FALSE(client->closed);
    const auto batches = client->all<wire::EventBatch>();
    CHECK(batches.size() == contracts::ipc::kMaxSubscriptions);
    for (const auto& batch : batches) {
        CHECK(batch.sub_id >= 10);
        CHECK(batch.sub_id < 10 + contracts::ipc::kMaxSubscriptions);
        CHECK(batch.events.size() == 1);
    }
}

TEST_CASE("a subscription that loses an event gets Resync", "[ipc][server][events]") {
    Fixture f;
    auto client = f.greeted();
    client->send(wire::Subscribe{1, {}});
    f.run();
    f.engine.events.publish(EventKind::SessionEnded, TestEvent{bytes_of("huge"), ipc::kSubscriptionQueueBytes + 1});
    f.run();
    REQUIRE(client->all<wire::Resync>().size() == 1);
    CHECK(client->all<wire::Resync>().front().sub_id == 1);
    CHECK(client->all<wire::EventBatch>().empty());

    f.engine.events.publish(EventKind::SessionEnded, TestEvent{bytes_of("next")});
    f.run();
    CHECK(client->all<wire::EventBatch>().size() == 1);
}

TEST_CASE("a client over the outbound budget gets Resync, and twice within the window is dropped",
          "[ipc][server][events]") {
    Fixture f;
    auto client = f.greeted();
    client->send(wire::Subscribe{1, {}});
    f.run();

    const wire::Bytes mebibyte(std::size_t{1} << 20, 0x5A);
    // One strand task per event, so the subscription queue never holds more than one.
    const auto flood = [&](int count) {
        for (int i = 0; i < count; ++i) {
            f.engine.events.publish(EventKind::NoticeAdded, TestEvent{mebibyte});
            f.run();
        }
    };

    flood(32);
    std::size_t delivered = 0;
    for (const auto& batch : client->all<wire::EventBatch>()) delivered += batch.events.size();
    // Each frame is a little over 1 MiB, so 31 fit the 32 MiB budget.
    CHECK(delivered == 31);
    REQUIRE(client->all<wire::Resync>().size() == 1);
    CHECK_FALSE(client->closed);

    f.engine.executor.advance(contracts::ipc::kSlowConsumerWindow - 1s);
    flood(1);
    CHECK(client->closed);
    REQUIRE_FALSE(client->all<wire::Goodbye>().empty());
    CHECK(client->all<wire::Goodbye>().back().reason == wire::GoodbyeReason::SlowConsumer);
}

TEST_CASE("credit returns outbound budget so a steady consumer is never dropped", "[ipc][server][events]") {
    Fixture f;
    auto client = f.greeted();
    client->send(wire::Subscribe{1, {}});
    f.run();

    const wire::Bytes mebibyte(std::size_t{1} << 20, 0x5A);
    std::size_t credited = 0;
    for (int round = 0; round < 6; ++round) {
        for (int i = 0; i < 20; ++i) {
            f.engine.events.publish(EventKind::NoticeAdded, TestEvent{mebibyte});
            f.run();
        }
        std::size_t delivered = 0;
        for (const auto& batch : client->all<wire::EventBatch>()) delivered += batch.events.size();
        client->send(wire::Credit{1, static_cast<u32>(delivered - credited)});
        credited = delivered;
        f.run();
    }
    CHECK(credited == 120);
    CHECK(client->all<wire::Resync>().empty());
    CHECK_FALSE(client->closed);
}

TEST_CASE("a Reply over kIpcFrameCap is answered with ipc.message_too_large", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted();
    client->send(wire::Call{1, kHugeMethod, {}, 0});
    client->send(wire::Call{2, kMethod, bytes_of("x"), 0});
    f.run();
    const auto replies = client->all<wire::Reply>();
    REQUIRE(replies.size() == 2);
    REQUIRE(replies[0].error);
    CHECK(replies[0].error->id == "ipc.message_too_large");
    REQUIRE(replies[1].payload);
    CHECK(text(*replies[1].payload) == "re:x");
    CHECK_FALSE(client->closed);
}

TEST_CASE("an outcome over kIpcFrameCap reaches the client as Failed", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted();
    f.engine.api.huge_outcomes = true;
    client->send(wire::Start{1, kInstantMethod, {}, std::nullopt});
    client->send(wire::Start{2, kMethod, {}, std::nullopt});
    f.run();
    REQUIRE(f.engine.api.started.size() == 1);
    f.engine.api.started.front()->complete(Completed<void>{});
    f.run();

    const auto results = client->all<wire::OpResult>();
    REQUIRE(results.size() == 2);
    for (const auto& result : results) CHECK(text(result.outcome).ends_with(":failed:ipc.message_too_large"));
    CHECK_FALSE(client->closed);
}

TEST_CASE("another build is refused secrets and subscriptions", "[ipc][server]") {
    Fixture f;
    auto client = f.greeted("0.9.0+old");
    const SecretBytes put = ipc::IpcCodec::encode_secret_put(bytes_of("target"), SecretBytes{bytes_of("hunter2")});
    client->send_raw(put.reveal());
    client->send(wire::SecretReveal{5, kRevealableTarget});
    client->send(wire::Subscribe{1, {}});
    f.run();
    f.engine.events.publish(EventKind::NoticeAdded, TestEvent{bytes_of("x")});
    f.run();

    CHECK(f.engine.api.secrets.empty());
    const auto replies = client->all<wire::Reply>();
    REQUIRE(replies.size() == 1);
    REQUIRE(replies[0].error);
    CHECK(replies[0].error->id == "ipc.version_mismatch");
    CHECK(client->all<wire::EventBatch>().empty());
    CHECK_FALSE(client->closed);
}
