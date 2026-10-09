#pragma once

#include <catch2/catch_test_macros.hpp>

#include <any>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/ipc/api_dispatcher.hpp"
#include "reboot/ipc/ipc_codec.hpp"
#include "reboot/ipc/ipc_errors.hpp"
#include "reboot/ipc/ipc_server.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/testing/in_memory_ipc.hpp"

namespace rb::ipc::test {

namespace wire = contracts::ipc;

inline constexpr std::string_view kEndpoint = "reboot-engine-test";
inline constexpr std::string_view kBuild = "1.0.0+test";
inline constexpr u32 kMethod = wire::method_id(2, 1);
inline constexpr u32 kFailingMethod = wire::method_id(2, 2);
// start() completes the op before it returns.
inline constexpr u32 kInstantMethod = wire::method_id(2, 3);
// call() answers with a payload over kIpcFrameCap.
inline constexpr u32 kHugeMethod = wire::method_id(2, 4);
inline const wire::Bytes kBadFilter{0xFF};
inline const wire::Bytes kRevealableTarget{1};

[[nodiscard]] inline wire::Bytes bytes_of(std::string_view text) { return {text.begin(), text.end()}; }

[[nodiscard]] inline NativePath engine_exe() { return NativePath{"/app/reboot-engine"}; }
[[nodiscard]] inline NativePath data_root() { return NativePath{"/home/user/reboot"}; }

// The payload published in tests; encode_event carries `bytes` as the event payload.
struct TestEvent {
    wire::Bytes bytes;
    std::size_t extra_bytes = 0;

    [[nodiscard]] std::size_t approx_bytes() const { return bytes.size() + extra_bytes; }
};

// Covers no capability ids. The ApiRouter stand-in: echoes calls and encodes outcomes as text.
class FakeApiDispatcher final : public IApiDispatcher {
public:
    explicit FakeApiDispatcher(OpRegistry& ops) : ops_(ops) {}

    void on_connected(const ConnectionInfo& connection) override { connected.push_back(connection); }
    void on_disconnected(ConnectionId connection) override { disconnected.push_back(connection); }

    Result<wire::Bytes> call(const ConnectionInfo&, u32 method_id, std::span<const u8> request) override {
        calls.emplace_back(method_id, wire::Bytes(request.begin(), request.end()));
        if (method_id == kFailingMethod) return make_diag(ErrorDomain::Ipc, kUnknownOp).arg("op", u64{5}).fail();
        if (method_id == kHugeMethod) return wire::Bytes(kIpcFrameCap + 1, 0x41);
        wire::Bytes reply = bytes_of("re:");
        reply.insert(reply.end(), request.begin(), request.end());
        return reply;
    }

    Result<OpHandle> start(const ConnectionInfo&, u32 method_id, std::span<const u8>,
                           std::optional<DisconnectPolicy> disconnect) override {
        policies.push_back(disconnect);
        if (method_id == kFailingMethod) return make_diag(ErrorDomain::Ipc, kUnknownOp).arg("op", u64{6}).fail();
        auto [handle, op] = ops_.create<void>(OpKind::Generic, disconnect.value_or(DisconnectPolicy::Detached), {});
        if (method_id == kInstantMethod) op.complete(Completed<void>{});
        else started.push_back(&op);
        return handle;
    }

    wire::Bytes encode_outcome(OpId op, const ErasedOutcome& outcome) override {
        std::string text = std::to_string(op.value) + ":";
        if (huge_outcomes && std::holds_alternative<Completed<std::any>>(outcome)) return wire::Bytes(kIpcFrameCap, 0x41);
        if (std::holds_alternative<Completed<std::any>>(outcome)) text += "completed";
        else if (const auto* failed = std::get_if<Failed>(&outcome)) text += "failed:" + failed->error.id;
        else if (std::holds_alternative<Cancelled>(outcome)) text += "cancelled";
        else text += "timed_out";
        return bytes_of(text);
    }

    Result<EventFilter> decode_filter(std::span<const u8> filter) override {
        if (std::ranges::equal(filter, kBadFilter)) return make_diag(ErrorDomain::Ipc, kProtocolError).arg("frame_type", u64{0}).fail();
        EventFilter decoded;
        for (const u8 kind : filter) decoded.kinds.push_back(static_cast<EventKind>(kind));
        return decoded;
    }

    std::optional<wire::WireEvent> encode_event(const EventEnvelope& event) override {
        const auto* payload = std::any_cast<TestEvent>(&event.payload);
        if (payload == nullptr) return std::nullopt;
        return wire::WireEvent{static_cast<u32>(event.kind), event.epoch.value, event.seq, std::nullopt,
                               event.op ? std::optional<u64>(event.op->value) : std::nullopt, payload->bytes};
    }

    Result<void> put_secret(const ConnectionInfo&, std::span<const u8> target, SecretBytes secret) override {
        secrets.emplace_back(wire::Bytes(target.begin(), target.end()), secret.reveal());
        return {};
    }

    Result<SecretBytes> reveal_secret(const ConnectionInfo&, std::span<const u8> target) override {
        if (!std::ranges::equal(target, kRevealableTarget)) return make_diag(ErrorDomain::Ipc, kRevealRefused).fail();
        return SecretBytes{bytes_of("join-pw")};
    }

    std::vector<ConnectionInfo> connected;
    std::vector<ConnectionId> disconnected;
    std::vector<std::pair<u32, wire::Bytes>> calls;
    std::vector<std::optional<DisconnectPolicy>> policies;
    std::vector<Operation<void>*> started;
    std::vector<std::pair<wire::Bytes, std::vector<u8>>> secrets;
    // A Completed outcome encodes to kIpcFrameCap bytes, so its OpResult frame is over the cap.
    bool huge_outcomes = false;

private:
    OpRegistry& ops_;
};

// The engine half of every test: strand, timers, ops and an IpcServer over InMemoryIpc.
struct EngineHarness {
    explicit EngineHarness(ports::PeerIdentity self = {"1000", 4321})
        : ipc(executor, std::move(self)),
          listener(ipc.make_listener()),
          connector(ipc.make_connector()),
          server(std::make_unique<IpcServer>(IpcServerDeps{*listener, executor, clock, timers, ops, events, api},
                                             engine_hello())) {}

    [[nodiscard]] static EngineHello engine_hello() {
        return EngineHello{std::string(kBuild),
                           EngineEpoch{7},
                           4321,
                           engine_exe(),
                           data_root(),
                           wire::EngineOrigin::OnDemand,
                           wire::StorageMode::ReadWrite,
                           false};
    }

    ManualClock clock;
    ManualExecutor executor{clock};
    TimerService timers{clock, executor};
    EventBus events{EngineEpoch{7}};
    OpRegistry ops{clock, timers, events};
    FakeApiDispatcher api{ops};
    testing::InMemoryIpc ipc;
    std::unique_ptr<ports::IIpcListener> listener;
    std::unique_ptr<ports::IIpcConnector> connector;
    std::unique_ptr<IpcServer> server;
};

// A client speaking raw frames, to drive the engine end directly.
class RawClient {
public:
    explicit RawClient(std::unique_ptr<ports::IByteStream> stream) : stream_(std::move(stream)) {
        stream_->on_read([this](std::span<const u8> bytes) {
            auto decoded = codec_.feed_from_engine(bytes);
            REQUIRE(decoded.has_value());
            for (EngineMessage& message : *decoded) received.push_back(std::move(message));
        });
        stream_->on_close([this] { closed = true; });
    }

    template <class T>
    void send(const T& message) {
        const wire::Bytes frame = IpcCodec::encode(message);
        stream_->write(frame);
    }
    void send_raw(std::span<const u8> bytes) { stream_->write(bytes); }

    template <class T>
    [[nodiscard]] std::vector<T> all() const {
        std::vector<T> out;
        for (const EngineMessage& message : received)
            if (const T* typed = std::get_if<T>(&message)) out.push_back(*typed);
        return out;
    }

    std::vector<EngineMessage> received;
    bool closed = false;

private:
    std::unique_ptr<ports::IByteStream> stream_;
    IpcCodec codec_;
};

[[nodiscard]] inline wire::Hello hello(std::string build = std::string(kBuild)) {
    return wire::Hello{wire::ClientKind::Test, std::move(build), 1u << 16, 99, {"session-1", false, {}}};
}

}  // namespace rb::ipc::test
