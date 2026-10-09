#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "ipc_test_kit.hpp"
#include "reboot/ipc/ipc_connection.hpp"
#include "reboot/testing/memory_stream_pair.hpp"

using namespace rb;
using namespace rb::ipc::test;

namespace {

struct ConnectionFixture {
    ConnectionFixture() {
        testing::MemoryStreamPair pair =
            testing::make_memory_stream_pair(engine.executor, ports::PeerIdentity{"1000", 99}, {"1000", 4321});
        connection = std::make_shared<ipc::IpcConnection>(ConnectionId{3}, std::move(pair.a), engine.executor,
                                                          engine.clock, engine.api);
        client = std::make_unique<RawClient>(std::move(pair.b));
        connection->start([this](ipc::IpcConnection&, ipc::ClientMessage message) { messages.push_back(std::move(message)); },
                          [this](ipc::IpcConnection&) { ++closes; });
    }

    void run() { engine.executor.run_all(); }

    EngineHarness engine;
    std::shared_ptr<ipc::IpcConnection> connection;
    std::unique_ptr<RawClient> client;
    std::vector<ipc::ClientMessage> messages;
    int closes = 0;
};

}  // namespace

TEST_CASE("the connection reports its peer from the stream", "[ipc][connection]") {
    ConnectionFixture f;
    CHECK(f.connection->id() == ConnectionId{3});
    CHECK(f.connection->peer().user_id == "1000");
    CHECK(f.connection->peer().pid == 99);
    CHECK_FALSE(f.connection->greeted());
}

TEST_CASE("decoded messages reach the handler on the strand in order", "[ipc][connection]") {
    ConnectionFixture f;
    f.client->send(hello());
    f.client->send(contracts::common::Ping{1});
    CHECK(f.messages.empty());
    f.run();
    REQUIRE(f.messages.size() == 2);
    CHECK(std::holds_alternative<wire::Hello>(f.messages[0]));
    CHECK(std::holds_alternative<contracts::common::Ping>(f.messages[1]));
}

TEST_CASE("calls past kMaxOutstandingCalls are answered with ipc.too_many_calls", "[ipc][connection]") {
    ConnectionFixture f;
    const u64 limit = contracts::ipc::kMaxOutstandingCalls;
    for (u64 req = 1; req <= limit; ++req) f.client->send(wire::Call{req, kMethod, {}, 0});
    f.client->send(wire::Start{limit + 1, kMethod, {}, std::nullopt});
    f.run();

    CHECK(f.messages.size() == limit);
    auto replies = f.client->all<wire::Reply>();
    REQUIRE(replies.size() == 1);
    CHECK(replies[0].req_id == limit + 1);
    REQUIRE(replies[0].error);
    CHECK(replies[0].error->id == "ipc.too_many_calls");

    f.connection->finish_request();
    f.client->send(wire::Call{limit + 2, kMethod, {}, 0});
    f.run();
    CHECK(f.messages.size() == limit + 1);
    CHECK(f.client->all<wire::Reply>().size() == 1);
}

TEST_CASE("close sends Goodbye once and reports the close once, after the last message", "[ipc][connection]") {
    ConnectionFixture f;
    f.client->send(contracts::common::Ping{1});
    f.run();
    f.client->send(contracts::common::Ping{2});
    f.connection->close(wire::GoodbyeReason::Shutdown);
    f.connection->close(wire::GoodbyeReason::Normal);
    f.run();

    CHECK(f.messages.size() == 1);
    CHECK(f.closes == 1);
    REQUIRE(f.client->all<wire::Goodbye>().size() == 1);
    CHECK(f.client->all<wire::Goodbye>().front().reason == wire::GoodbyeReason::Shutdown);
    CHECK(f.client->closed);

    f.connection->send(contracts::common::Pong{3});
    f.run();
    CHECK(f.client->all<contracts::common::Pong>().empty());
}

TEST_CASE("the peer closing reports the close once", "[ipc][connection]") {
    ConnectionFixture f;
    f.client.reset();
    f.run();
    CHECK(f.closes == 1);
    f.connection->close(wire::GoodbyeReason::Normal);
    f.run();
    CHECK(f.closes == 1);
}

TEST_CASE("subscription bookkeeping fails with the ipc ids", "[ipc][connection]") {
    ConnectionFixture f;
    REQUIRE(f.connection->add_subscription(SubscriptionId{1}, f.engine.events.subscribe({}, 1024)));
    auto duplicate = f.connection->add_subscription(SubscriptionId{1}, f.engine.events.subscribe({}, 1024));
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.error().is(ipc::kDuplicateSubscription));

    for (u64 sub = 2; sub <= contracts::ipc::kMaxSubscriptions; ++sub)
        REQUIRE(f.connection->add_subscription(SubscriptionId{sub}, f.engine.events.subscribe({}, 1024)));
    auto excess = f.connection->add_subscription(SubscriptionId{1000}, f.engine.events.subscribe({}, 1024));
    REQUIRE_FALSE(excess);
    CHECK(excess.error().is(ipc::kTooManySubscriptions));

    auto unknown_credit = f.connection->credit(SubscriptionId{1000}, 1);
    REQUIRE_FALSE(unknown_credit);
    CHECK(unknown_credit.error().is(ipc::kUnknownSubscription));
    REQUIRE(f.connection->remove_subscription(SubscriptionId{1}));
    auto removed_twice = f.connection->remove_subscription(SubscriptionId{1});
    REQUIRE_FALSE(removed_twice);
    CHECK(removed_twice.error().is(ipc::kUnknownSubscription));
    CHECK(f.connection->add_subscription(SubscriptionId{1000}, f.engine.events.subscribe({}, 1024)));
}

TEST_CASE("attached ops are tracked until released", "[ipc][connection]") {
    ConnectionFixture f;
    f.connection->note_attached(OpId{4});
    f.connection->note_attached(OpId{2});
    CHECK(f.connection->has_attached(OpId{4}));
    CHECK(f.connection->attached_ops() == std::vector<OpId>{OpId{2}, OpId{4}});
    f.connection->note_released(OpId{4});
    CHECK_FALSE(f.connection->has_attached(OpId{4}));
    CHECK(f.connection->attached_ops() == std::vector<OpId>{OpId{2}});
}

TEST_CASE("reply_secret writes a Reply carrying the secret", "[ipc][connection]") {
    ConnectionFixture f;
    f.connection->reply_secret(9, SecretBytes{bytes_of("pw")});
    f.run();
    const auto replies = f.client->all<wire::Reply>();
    REQUIRE(replies.size() == 1);
    CHECK(replies[0].req_id == 9);
    REQUIRE(replies[0].payload);
    CHECK(*replies[0].payload == bytes_of("pw"));
}

TEST_CASE("a connection destroyed with a read in flight drops it", "[ipc][connection]") {
    ConnectionFixture f;
    f.client->send(contracts::common::Ping{1});
    f.connection.reset();
    f.run();
    CHECK(f.messages.empty());
    CHECK(f.closes == 0);
}
