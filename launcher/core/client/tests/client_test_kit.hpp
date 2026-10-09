#pragma once

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <any>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "client_context.hpp"
#include "completion_latch.hpp"
#include "connect_settings.hpp"
#include "engine_executable.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/ipc/api_dispatcher.hpp"
#include "reboot/ipc/endpoint.hpp"
#include "reboot/ipc/ipc_codec.hpp"
#include "reboot/ipc/ipc_errors.hpp"
#include "reboot/ipc/ipc_server.hpp"
#include "reboot/testing/fake_caller_context.hpp"
#include "reboot/testing/fake_engine_starter.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "reboot/testing/in_memory_ipc.hpp"

namespace rb::client::test {

namespace wire = contracts::ipc;

// An engine event kind the library passes through untouched.
inline constexpr u32 kEngineEventKind = 15;
inline constexpr u32 kMethod = wire::method_id(2, 1);
inline constexpr u32 kFailingMethod = wire::method_id(2, 2);
inline const wire::Bytes kRevealableTarget{1};
inline const ports::PeerIdentity kSelf{"1000", 77};
inline constexpr u32 kEnginePid = 4321;

[[nodiscard]] inline wire::Bytes bytes_of(std::string_view text) { return {text.begin(), text.end()}; }

[[nodiscard]] inline NativePath data_root() { return testing::default_fake_root() / "data"; }
[[nodiscard]] inline NativePath canonical_data_root() { return canonical_root(DataRoot{data_root(), true}); }

[[nodiscard]] inline std::string endpoint() {
    Result<std::string> name = ipc::endpoint_for(kSelf, root_hash16(canonical_data_root()));
    REQUIRE(name);
    return *name;
}

[[nodiscard]] inline NativePath engine_image() {
    return engine_executable(testing::FakePlatformPaths{testing::default_fake_root()});
}

[[nodiscard]] inline ConnectSettings settings(ipc::LaunchMode mode, std::chrono::milliseconds deadline) {
    ConnectSettings out;
    out.data_root = data_root();
    out.client_kind = wire::ClientKind::Test;
    out.launch_mode = mode;
    out.connect_deadline = deadline;
    return out;
}

[[nodiscard]] inline wire::WireEvent engine_event(u64 seq, u32 kind = kEngineEventKind) {
    wire::WireEvent event;
    event.kind = kind;
    event.epoch = 7;
    event.seq = seq;
    return event;
}

// The engine end as raw frames over InMemoryIpc: answers each Hello with `ack` and records the rest.
class ScriptedEngine {
public:
    ScriptedEngine(testing::InMemoryIpc& ipc, wire::HelloAck answer)
        : ack(std::move(answer)), listener_(ipc.make_listener()) {}

    void listen() {
        REQUIRE(listener_->listen(endpoint(), [this](std::unique_ptr<ports::IByteStream> stream) {
            accept(std::move(stream));
        }));
    }

    template <class T>
    void send(const T& message) {
        REQUIRE(stream_);
        stream_->write(ipc::IpcCodec::encode(message));
    }

    template <class T>
    [[nodiscard]] std::vector<T> all() const {
        std::vector<T> out;
        for (const ipc::ClientMessage& message : received)
            if (const T* typed = std::get_if<T>(&message)) out.push_back(*typed);
        return out;
    }

    template <class T>
    [[nodiscard]] T last() const {
        const std::vector<T> sent = all<T>();
        REQUIRE_FALSE(sent.empty());
        return sent.back();
    }

    wire::HelloAck ack;
    std::vector<ipc::ClientMessage> received;
    std::size_t connections = 0;

private:
    void accept(std::unique_ptr<ports::IByteStream> stream) {
        ++connections;
        stream_ = std::move(stream);
        auto codec = std::make_shared<ipc::IpcCodec>();
        stream_->on_read([this, codec](std::span<const u8> bytes) {
            auto decoded = codec->feed_from_client(bytes);
            REQUIRE(decoded);
            for (ipc::ClientMessage& message : *decoded) {
                if (std::holds_alternative<wire::Hello>(message)) stream_->write(ipc::IpcCodec::encode(ack));
                received.push_back(std::move(message));
            }
        });
        stream_->on_close([] {});
    }

    std::unique_ptr<ports::IIpcListener> listener_;
    std::unique_ptr<ports::IByteStream> stream_;
};

[[nodiscard]] inline wire::HelloAck hello_ack(u64 epoch = 7, wire::Compatibility compatibility = wire::Compatibility::Full) {
    return wire::HelloAck{REBOOT_BUILD_ID,
                          epoch,
                          kEnginePid,
                          to_wire(engine_image()),
                          to_wire(canonical_data_root()),
                          wire::EngineOrigin::OnDemand,
                          wire::StorageMode::ReadWrite,
                          true,
                          compatibility};
}

// One ClientContext on a ManualExecutor against a ScriptedEngine.
struct ContextFixture {
    explicit ContextFixture(ipc::LaunchMode launch = ipc::LaunchMode::Autostart, wire::HelloAck ack = hello_ack())
        : engine(ipc, std::move(ack)), mode(launch) {}

    [[nodiscard]] ClientDeps deps() {
        return ClientDeps{*connector, starter, caller, paths, files, clock, executor, executor, kSelf, std::nullopt};
    }

    // Listens, connects and returns what connect() reported.
    Result<Connected> connect() {
        engine.listen();
        auto created = ClientContext::create(deps(), settings(mode, std::chrono::seconds{2}));
        REQUIRE(created);
        context = std::move(*created);
        std::optional<Result<Connected>> result;
        context->connect([&result](Result<Connected> connected) { result = std::move(connected); });
        executor.run_all();
        REQUIRE(result);
        return std::move(*result);
    }

    void connected() { REQUIRE(connect()); }

    // Breaks the link as an engine crash does.
    void sever() {
        ipc.sever_all();
        executor.run_all();
    }

    ManualClock clock;
    ManualExecutor executor{clock};
    testing::InMemoryIpc ipc{executor, kSelf};
    std::unique_ptr<ports::IIpcConnector> connector = ipc.make_connector();
    testing::FakeEngineStarter starter;
    testing::FakeCallerContext caller;
    testing::FakePlatformPaths paths;
    testing::InMemoryFileSystem files{clock};
    ScriptedEngine engine;
    ipc::LaunchMode mode;
    std::unique_ptr<ClientContext> context;
};

// Payload of the events a LiveEngine publishes; encode_event carries `bytes` as the payload.
struct TestEvent {
    wire::Bytes bytes;
};

// The ApiRouter stand-in: echoes calls, completes ops on request and encodes outcomes as text.
class EchoDispatcher final : public ipc::IApiDispatcher {
public:
    explicit EchoDispatcher(OpRegistry& ops) : ops_(ops) {}

    void on_connected(const ipc::ConnectionInfo& connection) override { connected.push_back(connection); }
    void on_disconnected(ConnectionId) override {}

    Result<wire::Bytes> call(const ipc::ConnectionInfo&, u32 method_id, std::span<const u8> request) override {
        if (method_id == kFailingMethod) return make_diag(ErrorDomain::Ipc, ipc::kUnknownOp).arg("op", u64{5}).fail();
        wire::Bytes reply = bytes_of("re:");
        reply.insert(reply.end(), request.begin(), request.end());
        return reply;
    }

    Result<OpHandle> start(const ipc::ConnectionInfo&, u32 method_id, std::span<const u8>,
                           std::optional<DisconnectPolicy> disconnect) override {
        if (method_id == kFailingMethod) return make_diag(ErrorDomain::Ipc, ipc::kUnknownOp).arg("op", u64{6}).fail();
        auto [handle, op] = ops_.create<void>(OpKind::Generic, disconnect.value_or(DisconnectPolicy::Detached), {});
        started.push_back(&op);
        return handle;
    }

    wire::Bytes encode_outcome(OpId op, const ErasedOutcome& outcome) override {
        std::string text = std::to_string(op.value) + ":";
        if (std::holds_alternative<Completed<std::any>>(outcome)) text += "completed";
        else if (std::holds_alternative<Cancelled>(outcome)) text += "cancelled";
        else text += "other";
        return bytes_of(text);
    }

    Result<EventFilter> decode_filter(std::span<const u8>) override { return EventFilter{}; }

    std::optional<wire::WireEvent> encode_event(const EventEnvelope& event) override {
        const auto* payload = std::any_cast<TestEvent>(&event.payload);
        if (payload == nullptr) return std::nullopt;
        return wire::WireEvent{kEngineEventKind, event.epoch.value, event.seq, std::nullopt, std::nullopt, payload->bytes};
    }

    Result<void> put_secret(const ipc::ConnectionInfo&, std::span<const u8> target, SecretBytes secret) override {
        secrets.emplace_back(wire::Bytes(target.begin(), target.end()), secret.reveal());
        return {};
    }

    Result<SecretBytes> reveal_secret(const ipc::ConnectionInfo&, std::span<const u8> target) override {
        if (!std::ranges::equal(target, kRevealableTarget)) return make_diag(ErrorDomain::Ipc, ipc::kRevealRefused).fail();
        return SecretBytes{bytes_of("join-pw")};
    }

    std::vector<ipc::ConnectionInfo> connected;
    std::vector<Operation<void>*> started;
    std::vector<std::pair<wire::Bytes, std::vector<u8>>> secrets;

private:
    OpRegistry& ops_;
};

// A real IpcServer on its own Strand thread and the system clock, reached over InMemoryIpc.
struct LiveEngine {
    explicit LiveEngine(std::string build = REBOOT_BUILD_ID, NativePath image = engine_image())
        : server(std::make_unique<ipc::IpcServer>(
              ipc::IpcServerDeps{*listener, strand, clock, timers, ops, events, api},
              ipc::EngineHello{std::move(build), EngineEpoch{7}, kEnginePid, std::move(image), canonical_data_root(),
                               wire::EngineOrigin::OnDemand, wire::StorageMode::ReadWrite, true})),
          thread([this] { strand.run(); }) {}

    ~LiveEngine() {
        on_strand([this] { server.reset(); });
        strand.stop();
        thread.join();
    }

    LiveEngine(const LiveEngine&) = delete;
    LiveEngine& operator=(const LiveEngine&) = delete;

    // Runs `task` on the engine strand and waits for it.
    template <class F>
    void on_strand(F task) {
        CompletionLatch<bool> latch;
        strand.post([&] {
            task();
            latch.set(true);
        });
        static_cast<void>(latch.wait());
    }

    // No Catch2 assertion, so an autostart hook may call it from a library thread.
    [[nodiscard]] bool listen() {
        bool started = false;
        on_strand([&] { started = server->start(name).has_value(); });
        return started;
    }

    void publish(wire::Bytes bytes) {
        on_strand([this, &bytes] { events.publish(EventKind::SessionEnded, TestEvent{bytes}); });
    }

    void complete_last_op() {
        bool completed = false;
        on_strand([&] { completed = !api.started.empty() && api.started.back()->complete(Completed<void>{}); });
        REQUIRE(completed);
    }

    const std::string name = endpoint();
    SystemClock clock;
    Strand strand;
    TimerService timers{clock, strand};
    EventBus events{EngineEpoch{7}};
    OpRegistry ops{clock, timers, events};
    EchoDispatcher api{ops};
    testing::InMemoryIpc ipc{strand, kSelf};
    std::unique_ptr<ports::IIpcListener> listener = ipc.make_listener();
    std::unique_ptr<ipc::IpcServer> server;
    std::thread thread;
};

}  // namespace rb::client::test
