#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ipc_test_kit.hpp"
#include "reboot/ipc/ipc_client.hpp"
#include "reboot/ipc/ipc_limits.hpp"
#include "reboot/testing/fake_engine_starter.hpp"
#include "reboot/testing/in_memory_file_system.hpp"

using namespace rb;
using namespace rb::ipc::test;
using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""s;

namespace {

struct Loss {
    std::string id;
    ipc::LinkLoss loss{};
};

class RecordingSink final : public ipc::IIpcClientSink {
public:
    void on_frame(ipc::EngineMessage message) override { frames.push_back(std::move(message)); }
    void on_lost(const Diagnostic& reason, ipc::LinkLoss loss) override {
        losses.push_back(Loss{reason.id, loss});
        if (on_lost_hook) on_lost_hook();
    }
    void on_reconnected(const ipc::Handshake& handshake) override { reconnects.push_back(handshake); }

    template <class T>
    [[nodiscard]] std::vector<T> all() const {
        std::vector<T> out;
        for (const ipc::EngineMessage& message : frames)
            if (const T* typed = std::get_if<T>(&message)) out.push_back(*typed);
        return out;
    }

    std::vector<ipc::EngineMessage> frames;
    std::vector<Loss> losses;
    std::vector<ipc::Handshake> reconnects;
    UniqueFunction<void()> on_lost_hook;
};

struct ClientFixture {
    explicit ClientFixture(ipc::LaunchMode mode = ipc::LaunchMode::Autostart) {
        starter.on_started([this](const NativePath&, const DataRoot&) { REQUIRE(engine.server->start(kEndpoint)); });
        client = make_client(mode);
    }

    [[nodiscard]] ipc::IpcClientOptions options(ipc::LaunchMode mode) const {
        return ipc::IpcClientOptions{
            .endpoint = std::string(kEndpoint),
            .engine_exe = engine_exe(),
            .data_root = DataRoot{data_root(), true},
            .canonical_root = data_root(),
            .update_marker = marker,
            .hello = hello(),
            .launch_mode = mode,
            .connect_deadline = 2s,
        };
    }

    [[nodiscard]] std::unique_ptr<ipc::IpcClient> make_client(ipc::LaunchMode mode) {
        return std::make_unique<ipc::IpcClient>(
            ipc::IpcClientDeps{*engine.connector, starter, files, engine.clock, engine.executor, sink}, options(mode));
    }

    // Runs connect() to its end and returns what `done` received.
    Result<ipc::Handshake> connect(std::chrono::milliseconds budget = 3s) {
        std::optional<Result<ipc::Handshake>> result;
        client->connect([&result](Result<ipc::Handshake> handshake) { result = std::move(handshake); });
        engine.executor.advance(budget);
        REQUIRE(result.has_value());
        return std::move(*result);
    }

    void run() { engine.executor.run_all(); }

    EngineHarness engine;
    testing::FakeEngineStarter starter;
    testing::InMemoryFileSystem files{engine.clock};
    RecordingSink sink;
    NativePath marker = data_root() / "state" / "update-in-progress";
    std::unique_ptr<ipc::IpcClient> client;
};

}  // namespace

TEST_CASE("autostart starts the engine once and completes the handshake", "[ipc][client]") {
    ClientFixture f;
    auto handshake = f.connect();
    REQUIRE(handshake.has_value());
    CHECK(f.starter.calls() == 1);
    CHECK(handshake->hello.engine_build == kBuild);
    CHECK(handshake->hello.epoch == 7);
    CHECK(handshake->hello.compatibility == wire::Compatibility::Full);
    CHECK_FALSE(handshake->image_warning);
    REQUIRE(f.engine.api.connected.size() == 1);
    CHECK(f.engine.api.connected.front().client_pid == 99);
}

TEST_CASE("an engine already listening is reached without the starter", "[ipc][client]") {
    ClientFixture f;
    REQUIRE(f.engine.server->start(kEndpoint));
    REQUIRE(f.connect().has_value());
    CHECK(f.starter.calls() == 0);
}

TEST_CASE("ConnectOnly never starts an engine and times out", "[ipc][client]") {
    ClientFixture f(ipc::LaunchMode::ConnectOnly);
    auto result = f.connect();
    REQUIRE_FALSE(result);
    CHECK(result.error().is(ipc::kEngineUnavailable));
    CHECK(result.error().kind == ErrorKind::EngineUnavailable);
    CHECK(f.starter.calls() == 0);
}

TEST_CASE("connect reports within the deadline", "[ipc][client]") {
    ClientFixture f(ipc::LaunchMode::ConnectOnly);
    std::optional<Result<ipc::Handshake>> result;
    f.client->connect([&result](Result<ipc::Handshake> handshake) { result = std::move(handshake); });
    f.engine.executor.advance(2s - 1ms);
    CHECK_FALSE(result);
    f.engine.executor.advance(1ms);
    CHECK(result);
}

TEST_CASE("a fresh update marker holds the starter back", "[ipc][client]") {
    ClientFixture f;
    f.files.write_text(f.marker, "{}");
    auto result = f.connect();
    REQUIRE_FALSE(result);
    CHECK(result.error().is(ipc::kUpdateInProgress));
    CHECK(f.starter.calls() == 0);
}

TEST_CASE("an engine brought up by the update is reached while the marker stands", "[ipc][client]") {
    ClientFixture f;
    f.files.write_text(f.marker, "{}");
    std::optional<Result<ipc::Handshake>> result;
    f.client->connect([&result](Result<ipc::Handshake> handshake) { result = std::move(handshake); });
    f.engine.executor.advance(500ms);
    REQUIRE(f.engine.server->start(kEndpoint));
    f.engine.executor.advance(500ms);
    REQUIRE(result);
    CHECK(result->has_value());
    CHECK(f.starter.calls() == 0);
}

TEST_CASE("a stale update marker no longer holds the starter back", "[ipc][client]") {
    ClientFixture f;
    f.files.write_text(f.marker, "{}");
    f.engine.clock.advance(ipc::kUpdateMarkerTimeout + 1s);
    REQUIRE(f.connect().has_value());
    CHECK(f.starter.calls() == 1);
}

TEST_CASE("starter refusals end connect with their ids", "[ipc][client]") {
    const std::vector<std::pair<ports::StartResult, MessageId>> cases{
        {ports::StartResult::CannotDetach, ipc::kEngineCannotDetach},
        {ports::StartResult::ElevatedRefused, ipc::kElevatedAutostartRefused},
        {ports::StartResult::NoInteractiveSession, ipc::kNoInteractiveSession},
        {ports::StartResult::AwaitingUser, ipc::kAgentRequiresApproval},
    };
    for (const auto& [answer, id] : cases) {
        ClientFixture f;
        f.starter.script({answer});
        std::optional<Result<ipc::Handshake>> result;
        f.client->connect([&result](Result<ipc::Handshake> handshake) { result = std::move(handshake); });
        f.run();
        REQUIRE(result);
        REQUIRE_FALSE(result->has_value());
        CHECK(result->error().is(id));
    }
}

TEST_CASE("a starter error ends connect with it", "[ipc][client]") {
    ClientFixture f;
    f.starter.script({make_diag(ErrorDomain::Ipc, ipc::kEngineCannotDetach).fail()});
    auto result = f.connect();
    REQUIRE_FALSE(result);
    CHECK(result.error().is(ipc::kEngineCannotDetach));
}

TEST_CASE("an endpoint owned by another user is refused without starting anything", "[ipc][client]") {
    ClientFixture f;
    f.engine.ipc.squat(std::string(kEndpoint), ports::PeerIdentity{"2000", 1});
    auto result = f.connect();
    REQUIRE_FALSE(result);
    CHECK(result.error().is(ipc::kEndpointUntrusted));
    CHECK(f.starter.calls() == 0);
}

TEST_CASE("an engine serving another root is refused", "[ipc][client]") {
    ClientFixture f;
    ipc::EngineHello other = EngineHarness::engine_hello();
    other.canonical_root = NativePath{"/elsewhere"};
    f.engine.server = std::make_unique<ipc::IpcServer>(
        ipc::IpcServerDeps{*f.engine.listener, f.engine.executor, f.engine.clock, f.engine.timers, f.engine.ops,
                           f.engine.events, f.engine.api},
        other);
    REQUIRE(f.engine.server->start(kEndpoint));
    auto result = f.connect();
    REQUIRE_FALSE(result);
    CHECK(result.error().is(ipc::kRootMismatch));
}

TEST_CASE("an engine running from another image connects with a warning", "[ipc][client]") {
    ClientFixture f;
    ipc::EngineHello other = EngineHarness::engine_hello();
    other.image_path = NativePath{"/old/reboot-engine"};
    f.engine.server = std::make_unique<ipc::IpcServer>(
        ipc::IpcServerDeps{*f.engine.listener, f.engine.executor, f.engine.clock, f.engine.timers, f.engine.ops,
                           f.engine.events, f.engine.api},
        other);
    REQUIRE(f.engine.server->start(kEndpoint));
    auto result = f.connect();
    REQUIRE(result.has_value());
    REQUIRE(result->image_warning);
    CHECK(result->image_warning->is(ipc::kEngineImageDiffers));
    CHECK(result->image_warning->severity == Severity::Warning);
}

TEST_CASE("send fails with connection_lost until the link is up, then reaches the engine", "[ipc][client]") {
    ClientFixture f;
    auto early = f.client->send(wire::Call{1, kMethod, bytes_of("x"), 0});
    REQUIRE_FALSE(early);
    CHECK(early.error().is(ipc::kConnectionLost));

    REQUIRE(f.connect().has_value());
    REQUIRE(f.client->send(wire::Call{1, kMethod, bytes_of("x"), 0}));
    f.run();
    const auto replies = f.sink.all<wire::Reply>();
    REQUIRE(replies.size() == 1);
    REQUIRE(replies[0].payload);
    CHECK(*replies[0].payload == bytes_of("re:x"));
}

TEST_CASE("send_secret_put reaches the engine", "[ipc][client]") {
    ClientFixture f;
    REQUIRE(f.connect().has_value());
    REQUIRE(f.client->send_secret_put(bytes_of("acct"), SecretBytes{bytes_of("hunter2")}));
    f.run();
    REQUIRE(f.engine.api.secrets.size() == 1);
    CHECK(f.engine.api.secrets[0].second == bytes_of("hunter2"));
}

TEST_CASE("frames after the handshake reach the sink, including a later HelloAck", "[ipc][client]") {
    ClientFixture f;
    REQUIRE(f.connect().has_value());
    f.engine.server->set_secrets_available(true);
    f.engine.server->send_foreground_hint(31);
    f.run();
    REQUIRE(f.sink.all<wire::HelloAck>().size() == 1);
    CHECK(f.sink.all<wire::HelloAck>().front().secrets_available);
    CHECK(f.sink.all<wire::ForegroundHint>().front().pid == 31);
}

TEST_CASE("a lost link is reported, then reconnected after the backoff", "[ipc][client]") {
    ClientFixture f;
    REQUIRE(f.connect().has_value());
    f.engine.ipc.sever_all();
    f.run();
    REQUIRE(f.sink.losses.size() == 1);
    CHECK(f.sink.losses[0].id == "ipc.connection_lost");
    CHECK(f.sink.losses[0].loss == ipc::LinkLoss::Reconnecting);
    CHECK(f.client->send(contracts::common::Ping{1}).error().is(ipc::kConnectionLost));

    f.engine.executor.advance(ipc::kReconnectBackoffMin - 1ms);
    CHECK(f.sink.reconnects.empty());
    f.engine.executor.advance(1ms);
    REQUIRE(f.sink.reconnects.size() == 1);
    CHECK(f.sink.reconnects[0].hello.epoch == 7);
    CHECK(f.client->send(contracts::common::Ping{1}));
    CHECK(f.engine.api.connected.size() == 2);
}

TEST_CASE("a reconnect restarts a crashed engine through the starter", "[ipc][client]") {
    ClientFixture f;
    REQUIRE(f.connect().has_value());
    REQUIRE(f.starter.calls() == 1);
    f.engine.server->stop(wire::GoodbyeReason::Shutdown);
    f.run();
    REQUIRE(f.sink.losses.size() == 1);
    CHECK(f.sink.losses[0].id == "ipc.engine_closed");
    CHECK(f.sink.losses[0].loss == ipc::LinkLoss::Reconnecting);

    f.engine.executor.advance(2s);
    CHECK(f.starter.calls() == 2);
    CHECK(f.sink.reconnects.size() == 1);
}

TEST_CASE("reconnect attempts back off up to the maximum until the engine returns", "[ipc][client]") {
    ClientFixture f;
    REQUIRE(f.connect().has_value());
    f.starter.script({ports::StartResult::ConnectOnly});
    f.engine.server->stop(wire::GoodbyeReason::Shutdown);
    f.run();

    f.engine.executor.advance(60s);
    CHECK(f.sink.reconnects.empty());
    CHECK(f.sink.losses.size() == 1);

    REQUIRE(f.engine.server->start(kEndpoint));
    f.engine.executor.advance(ipc::kReconnectBackoffMax + 3s);
    CHECK(f.sink.reconnects.size() == 1);
}

TEST_CASE("a Goodbye the client cannot recover from is a final loss", "[ipc][client]") {
    ClientFixture f;
    REQUIRE(f.connect().has_value());
    f.engine.server->stop(wire::GoodbyeReason::VersionMismatch);
    f.run();
    REQUIRE(f.sink.losses.size() == 1);
    CHECK(f.sink.losses[0].loss == ipc::LinkLoss::Final);
    f.engine.executor.advance(30s);
    CHECK(f.sink.reconnects.empty());
}

TEST_CASE("ConnectOnly reports every loss as final", "[ipc][client]") {
    ClientFixture f(ipc::LaunchMode::ConnectOnly);
    REQUIRE(f.engine.server->start(kEndpoint));
    REQUIRE(f.connect().has_value());
    f.engine.ipc.sever_all();
    f.run();
    REQUIRE(f.sink.losses.size() == 1);
    CHECK(f.sink.losses[0].loss == ipc::LinkLoss::Final);
    f.engine.executor.advance(30s);
    CHECK(f.sink.reconnects.empty());
}

TEST_CASE("an engine reconnect that finds another root is a final loss", "[ipc][client]") {
    ClientFixture f;
    REQUIRE(f.connect().has_value());
    f.engine.server->stop(wire::GoodbyeReason::Restarting);
    ipc::EngineHello other = EngineHarness::engine_hello();
    other.canonical_root = NativePath{"/elsewhere"};
    f.engine.server = std::make_unique<ipc::IpcServer>(
        ipc::IpcServerDeps{*f.engine.listener, f.engine.executor, f.engine.clock, f.engine.timers, f.engine.ops,
                           f.engine.events, f.engine.api},
        other);
    REQUIRE(f.engine.server->start(kEndpoint));
    f.engine.executor.advance(3s);
    REQUIRE(f.sink.losses.size() == 2);
    CHECK(f.sink.losses[1].id == "ipc.root_mismatch");
    CHECK(f.sink.losses[1].loss == ipc::LinkLoss::Final);
}

TEST_CASE("bad bytes from the engine are a protocol error loss", "[ipc][client]") {
    ClientFixture f;
    f.engine.server.reset();
    std::unique_ptr<ports::IByteStream> engine_end;
    REQUIRE(f.engine.listener->listen(kEndpoint, [&](std::unique_ptr<ports::IByteStream> stream) {
        engine_end = std::move(stream);
        const wire::Bytes ack = ipc::IpcCodec::encode(wire::HelloAck{std::string(kBuild), 7, 1, to_wire(engine_exe()),
                                                                     to_wire(data_root()), {}, {}, false, {}});
        engine_end->write(ack);
    }));
    REQUIRE(f.connect().has_value());
    const wire::Bytes garbage{0x3F, 0x00};
    engine_end->write(garbage);
    f.run();
    REQUIRE(f.sink.losses.size() == 1);
    CHECK(f.sink.losses[0].id == "ipc.protocol_error");
    CHECK(f.sink.losses[0].loss == ipc::LinkLoss::Reconnecting);
}

TEST_CASE("an engine that answers Hello with something else fails the handshake", "[ipc][client]") {
    ClientFixture f;
    std::unique_ptr<ports::IByteStream> engine_end;
    REQUIRE(f.engine.listener->listen(kEndpoint, [&](std::unique_ptr<ports::IByteStream> stream) {
        engine_end = std::move(stream);
        engine_end->write(ipc::IpcCodec::encode(wire::Resync{1}));
    }));
    auto result = f.connect();
    REQUIRE_FALSE(result);
    CHECK(result.error().is(ipc::kProtocolError));
}

TEST_CASE("an engine that never answers Hello times out", "[ipc][client]") {
    ClientFixture f(ipc::LaunchMode::ConnectOnly);
    std::unique_ptr<ports::IByteStream> engine_end;
    REQUIRE(f.engine.listener->listen(kEndpoint,
                                      [&](std::unique_ptr<ports::IByteStream> stream) { engine_end = std::move(stream); }));
    auto result = f.connect();
    REQUIRE_FALSE(result);
    CHECK(result.error().is(ipc::kEngineUnavailable));
}

TEST_CASE("close says Goodbye, stops reconnecting and silences the sink", "[ipc][client]") {
    ClientFixture f;
    REQUIRE(f.connect().has_value());
    f.client->close();
    f.client->close();
    f.run();
    CHECK(f.engine.server->connection_count() == 0);
    CHECK(f.sink.losses.empty());
    f.engine.executor.advance(10s);
    CHECK(f.sink.reconnects.empty());
    CHECK(f.client->send(contracts::common::Ping{1}).error().is(ipc::kConnectionLost));

    std::optional<Result<ipc::Handshake>> again;
    f.client->connect([&again](Result<ipc::Handshake> handshake) { again = std::move(handshake); });
    f.run();
    REQUIRE(again);
    CHECK(again->error().is(ipc::kConnectionLost));
}

TEST_CASE("close during connect fails the pending connect once", "[ipc][client]") {
    ClientFixture f(ipc::LaunchMode::ConnectOnly);
    int calls = 0;
    std::optional<Result<ipc::Handshake>> result;
    f.client->connect([&](Result<ipc::Handshake> handshake) {
        ++calls;
        result = std::move(handshake);
    });
    f.engine.executor.advance(500ms);
    f.client->close();
    f.engine.executor.advance(5s);
    CHECK(calls == 1);
    REQUIRE(result);
    CHECK(result->error().is(ipc::kConnectionLost));
}

TEST_CASE("connect while connected reports the current handshake", "[ipc][client]") {
    ClientFixture f;
    REQUIRE(f.connect().has_value());
    auto again = f.connect();
    REQUIRE(again.has_value());
    CHECK(again->hello.pid == 4321);
    CHECK(f.engine.api.connected.size() == 1);
}

TEST_CASE("the client may be destroyed from inside a sink call", "[ipc][client]") {
    ClientFixture f;
    REQUIRE(f.connect().has_value());
    f.sink.on_lost_hook = [&f] { f.client.reset(); };
    f.engine.ipc.sever_all();
    f.run();
    CHECK_FALSE(f.client);
    REQUIRE(f.sink.losses.size() == 1);
    f.engine.executor.advance(10s);
    CHECK(f.sink.reconnects.empty());
}

TEST_CASE("a destroyed client leaves its posted work harmless", "[ipc][client]") {
    ClientFixture f;
    int calls = 0;
    f.client->connect([&calls](Result<ipc::Handshake>) { ++calls; });
    f.client.reset();
    f.engine.executor.advance(5s);
    CHECK(calls == 0);
    CHECK(f.sink.losses.empty());
}

TEST_CASE("a frame over kIpcFrameCap is refused before it is sent and the link stays up", "[ipc][client]") {
    ClientFixture f;
    REQUIRE(f.connect().has_value());
    auto sent = f.client->send(wire::Call{1, kMethod, wire::Bytes(kIpcFrameCap + 1, 0x41), 0});
    REQUIRE_FALSE(sent);
    CHECK(sent.error().is(ipc::kMessageTooLarge));
    CHECK(sent.error().kind == ErrorKind::InvalidInput);
    CHECK(f.client->send_secret_put(bytes_of("t"), SecretBytes{wire::Bytes(kIpcFrameCap, 0x41)}).error().is(
        ipc::kMessageTooLarge));

    REQUIRE(f.client->send(wire::Call{2, kMethod, bytes_of("x"), 0}));
    f.run();
    CHECK(f.sink.losses.empty());
    CHECK(f.engine.api.calls.size() == 1);
    CHECK(f.sink.all<wire::Reply>().size() == 1);
}

TEST_CASE("connect during a reconnect backoff starts the reconnect at once", "[ipc][client]") {
    ClientFixture f;
    REQUIRE(f.connect().has_value());
    f.engine.server->stop(wire::GoodbyeReason::Restarting);
    f.run();
    REQUIRE(f.sink.losses.size() == 1);
    REQUIRE(f.engine.server->start(kEndpoint));

    std::optional<Result<ipc::Handshake>> result;
    f.client->connect([&result](Result<ipc::Handshake> handshake) { result = std::move(handshake); });
    f.run();
    REQUIRE(result);
    CHECK(result->has_value());
    CHECK(f.sink.reconnects.size() == 1);
}
