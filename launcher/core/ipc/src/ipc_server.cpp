#include "reboot/ipc/ipc_server.hpp"

#include <algorithm>
#include <any>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "reboot/contracts/common.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/ipc/api_dispatcher.hpp"
#include "reboot/ipc/compatibility.hpp"
#include "reboot/ipc/ipc_connection.hpp"
#include "reboot/ipc/ipc_errors.hpp"
#include "reboot/ipc/ipc_limits.hpp"
#include "reboot/ports/ipc.hpp"

namespace reboot::ipc {

namespace {

namespace wire = contracts::ipc;

template <class... F>
struct Overloaded : F... {
    using F::operator()...;
};

[[nodiscard]] Diagnostic too_large(std::size_t size) {
    return make_diag(ErrorDomain::Ipc, kMessageTooLarge)
        .arg("size", size)
        .arg("limit", kIpcFrameCap)
        .kind(ErrorKind::InvalidInput)
        .build();
}

}  // namespace

struct IpcServer::Impl {
    Impl(IpcServerDeps server_deps, EngineHello engine_hello)
        : deps(server_deps), hello(std::move(engine_hello)) {}

    IpcServerDeps deps;
    EngineHello hello;
    std::map<ConnectionId, std::shared_ptr<IpcConnection>> connections;
    std::map<ConnectionId, TimerHandle> hello_timers;
    u64 next_connection = 1;
    bool stopped = false;
    std::shared_ptr<Subscription> op_events;
    // Strand tasks and connection handlers check this before touching the server.
    std::shared_ptr<char> alive = std::make_shared<char>();

    [[nodiscard]] wire::HelloAck hello_ack(Compatibility compatibility) const {
        return wire::HelloAck{hello.engine_build,          hello.epoch.value, hello.pid, to_wire(hello.image_path),
                              to_wire(hello.canonical_root), hello.origin,      hello.storage_mode,
                              hello.secrets_available,       compatibility};
    }

    void accept(std::unique_ptr<ports::IByteStream> stream) {
        if (stopped) {
            stream->close();
            return;
        }
        const ConnectionId id{next_connection++};
        auto connection = std::make_shared<IpcConnection>(id, std::move(stream), deps.strand, deps.clock, deps.api);
        connections.emplace(id, connection);
        hello_timers.emplace(id, deps.timers.after(kHelloDeadline, [this, id] {
            const auto it = connections.find(id);
            if (it == connections.end() || it->second->greeted()) return;
            REBOOT_LOG_INFO(Ipc, "connection {} sent no Hello in time", id.value);
            it->second->close(wire::GoodbyeReason::ProtocolError);
        }));
        const std::weak_ptr<char> token = alive;
        connection->start(
            [this, token](IpcConnection& from, ClientMessage message) {
                if (!token.expired()) on_message(from, std::move(message));
            },
            [this, token](IpcConnection& from) {
                if (!token.expired()) on_closed(from);
            });
    }

    void on_closed(IpcConnection& connection) {
        const ConnectionId id = connection.id();
        hello_timers.erase(id);
        deps.ops.on_connection_closed(id);
        if (connection.greeted()) deps.api.on_disconnected(id);
        connections.erase(id);
    }

    void on_message(IpcConnection& connection, ClientMessage message) {
        if (!connection.greeted()) {
            if (auto* hello_message = std::get_if<wire::Hello>(&message)) {
                greet(connection, std::move(*hello_message));
            } else if (std::holds_alternative<wire::Goodbye>(message)) {
                connection.close(wire::GoodbyeReason::Normal);
            } else {
                if (auto* put = std::get_if<wire::SecretPut>(&message)) secure_wipe(put->bytes.data(), put->bytes.size());
                connection.close(wire::GoodbyeReason::ProtocolError);
            }
            return;
        }
        std::visit(Overloaded{
                       [&](wire::Hello&) { connection.close(wire::GoodbyeReason::ProtocolError); },
                       [&](wire::Call& call) { on_call(connection, call); },
                       [&](wire::Start& start) { on_start(connection, start); },
                       [&](wire::Cancel& cancel) {
                           if (auto cancelled = deps.ops.cancel(OpId{cancel.op_id}, CancelReason::User); !cancelled)
                               REBOOT_LOG_DEBUG(Ipc, "cancel of op {}: {}", cancel.op_id, cancelled.error().id);
                       },
                       [&](wire::Attach& attach) { on_attach(connection, OpId{attach.op_id}); },
                       [&](wire::Release& release) {
                           deps.ops.release(OpId{release.op_id}, connection.id());
                           connection.note_released(OpId{release.op_id});
                       },
                       [&](wire::Subscribe& subscribe) { on_subscribe(connection, subscribe); },
                       [&](wire::Unsubscribe& unsubscribe) {
                           static_cast<void>(connection.remove_subscription(SubscriptionId{unsubscribe.sub_id}));
                       },
                       [&](wire::Credit& credit) {
                           static_cast<void>(connection.credit(SubscriptionId{credit.sub_id}, credit.n));
                       },
                       [&](wire::SecretPut& put) {
                           SecretBytes secret{std::move(put.bytes)};
                           // The target is reboot.api.v1 bytes, which only the same build reads right.
                           Result<void> stored = connection.info().compatibility == Compatibility::Full
                                                     ? deps.api.put_secret(connection.info(), put.target, std::move(secret))
                                                     : std::unexpected(version_mismatch(contract_frame_type_v<wire::SecretPut>));
                           if (!stored)
                               REBOOT_LOG_WARN(Ipc, "a secret from connection {} was not stored: {}",
                                               connection.id().value, stored.error().id);
                       },
                       [&](wire::SecretReveal& reveal) {
                           Result<SecretBytes> secret =
                               connection.info().compatibility == Compatibility::Full
                                   ? deps.api.reveal_secret(connection.info(), reveal.target)
                                   : std::unexpected(version_mismatch(contract_frame_type_v<wire::SecretReveal>));
                           if (secret) connection.reply_secret(reveal.req_id, std::move(*secret));
                           else connection.send(wire::Reply{reveal.req_id, std::nullopt,
                                                            contracts::common::to_wire(secret.error())});
                       },
                       [&](wire::LogWrite& log) {
                           // A level past Error would escape the logger's rules for dropping.
                           const LogLevel level = std::min(log.level, LogLevel::Error);
                           if (Logger::enabled(level))
                               Logger::write(level, LogCategory::Client, std::nullopt,
                                             std::format("[client {}] {}", connection.info().client_pid, log.text));
                       },
                       [&](contracts::common::Ping& ping) { connection.send(contracts::common::Pong{ping.nonce}); },
                       [&](wire::Goodbye&) { connection.close(wire::GoodbyeReason::Normal); },
                   },
                   message);
    }

    void greet(IpcConnection& connection, wire::Hello hello_message) {
        ConnectionInfo info{
            .id = connection.id(),
            .peer = connection.peer(),
            .client_kind = hello_message.client_kind,
            .client_build = std::move(hello_message.client_build),
            .abi_version = hello_message.abi_version,
            .client_pid = hello_message.pid,
            .caller = std::move(hello_message.caller_context),
        };
        info.compatibility = compatibility_for(hello.engine_build, info.client_build);
        hello_timers.erase(connection.id());
        connection.send(hello_ack(info.compatibility));
        connection.greet(info);
        deps.api.on_connected(info);
    }

    void on_call(IpcConnection& connection, const wire::Call& call) {
        const ConnectionInfo& info = connection.info();
        wire::Reply reply{call.req_id, std::nullopt, std::nullopt};
        if (!allows_method(info.compatibility, call.method_id)) {
            reply.error = contracts::common::to_wire(version_mismatch(call.method_id));
        } else if (Result<wire::Bytes> answer = deps.api.call(info, call.method_id, call.payload)) {
            reply.payload = std::move(*answer);
        } else {
            reply.error = contracts::common::to_wire(answer.error());
        }
        wire::Bytes frame = IpcCodec::encode(reply);
        // The client would drop the whole link over a frame it cannot take.
        if (!IpcCodec::fits_frame_cap(frame))
            frame = IpcCodec::encode(wire::Reply{call.req_id, std::nullopt, contracts::common::to_wire(too_large(frame.size()))});
        connection.send_frame(std::move(frame));
        connection.finish_request();
    }

    void on_start(IpcConnection& connection, const wire::Start& start) {
        const ConnectionInfo& info = connection.info();
        std::optional<Diagnostic> refused;
        if (!allows_method(info.compatibility, start.method_id)) {
            refused = version_mismatch(start.method_id);
        } else {
            std::optional<DisconnectPolicy> policy;
            if (start.detached)
                policy = *start.detached ? DisconnectPolicy::Detached : DisconnectPolicy::BoundToConnection;
            Result<OpHandle> handle = deps.api.start(info, start.method_id, start.payload, policy);
            if (handle) {
                const OpId op = handle->id();
                static_cast<void>(deps.ops.attach(op, connection.id()));
                connection.note_attached(op);
                connection.send(wire::Started{start.req_id, op.value});
                // An op that ended inside start() published its OpCompleted before this attach.
                if (std::optional<ErasedOutcome> outcome = deps.ops.outcome(op)) send_result(connection, op, *outcome);
            } else {
                refused = std::move(handle.error());
            }
        }
        if (refused) connection.send(wire::Reply{start.req_id, std::nullopt, contracts::common::to_wire(*refused)});
        connection.finish_request();
    }

    void on_attach(IpcConnection& connection, OpId op) {
        if (auto attached = deps.ops.attach(op, connection.id()); !attached) {
            // Attach has no reply, so an op the engine no longer knows ends as Failed.
            const Diagnostic unknown =
                make_diag(ErrorDomain::Ipc, kUnknownOp).arg("op", op.value).kind(ErrorKind::NotFound).build();
            connection.send(wire::OpResult{op.value, deps.api.encode_outcome(op, Failed{unknown})});
            return;
        }
        connection.note_attached(op);
        if (std::optional<ErasedOutcome> outcome = deps.ops.outcome(op)) send_result(connection, op, *outcome);
    }

    void on_subscribe(IpcConnection& connection, const wire::Subscribe& subscribe) {
        Result<EventFilter> filter = connection.info().compatibility == Compatibility::Full
                                         ? deps.api.decode_filter(subscribe.filter)
                                         : std::unexpected(version_mismatch(contract_frame_type_v<wire::Subscribe>));
        if (!filter) {
            REBOOT_LOG_WARN(Ipc, "subscription {} of connection {} dropped: {}", subscribe.sub_id,
                            connection.id().value, filter.error().id);
            return;
        }
        std::shared_ptr<Subscription> subscription = deps.events.subscribe(std::move(*filter), kSubscriptionQueueBytes);
        if (auto added = connection.add_subscription(SubscriptionId{subscribe.sub_id}, std::move(subscription)); !added)
            REBOOT_LOG_WARN(Ipc, "subscription {} of connection {} dropped: {}", subscribe.sub_id,
                            connection.id().value, added.error().id);
    }

    void send_result(IpcConnection& connection, OpId op, const ErasedOutcome& outcome) {
        connection.send_frame(op_result(op, outcome));
    }

    // An outcome too large for one frame reaches the client as Failed instead.
    [[nodiscard]] wire::Bytes op_result(OpId op, const ErasedOutcome& outcome) {
        wire::Bytes frame = IpcCodec::encode(wire::OpResult{op.value, deps.api.encode_outcome(op, outcome)});
        if (IpcCodec::fits_frame_cap(frame)) return frame;
        REBOOT_LOG_WARN(Ipc, "the outcome of op {} is {} bytes, too large to send", op.value, frame.size());
        return IpcCodec::encode(wire::OpResult{op.value, deps.api.encode_outcome(op, Failed{too_large(frame.size())})});
    }

    // Runs inside EventBus::publish, so an outcome reaches attached connections before anything
    // else on the strand can attach to the op and read it from the registry.
    void on_op_events() {
        std::vector<EventEnvelope> events;
        op_events->drain(events, std::numeric_limits<std::size_t>::max());
        for (const EventEnvelope& event : events) {
            const auto* completed = std::any_cast<OpCompletedEvent>(&event.payload);
            if (completed == nullptr) continue;
            std::optional<wire::Bytes> frame;
            for (const auto& [id, connection] : connections) {
                if (!connection->has_attached(completed->op)) continue;
                if (!frame) frame = op_result(completed->op, completed->outcome);
                connection->send_frame(*frame);
            }
        }
        if (!op_events->take_resync()) return;
        // Outcomes were dropped: every attached op that has ended is sent again; clients keep the first.
        for (const auto& [id, connection] : connections)
            for (const OpId op : connection->attached_ops())
                if (std::optional<ErasedOutcome> outcome = deps.ops.outcome(op)) send_result(*connection, op, *outcome);
    }

    [[nodiscard]] Diagnostic version_mismatch(u32 method_id) const {
        return make_diag(ErrorDomain::Ipc, kVersionMismatch)
            .arg("method", method_id)
            .arg("engine_build", hello.engine_build)
            .kind(ErrorKind::Unsupported)
            .build();
    }

    void close_all(wire::GoodbyeReason reason) {
        std::vector<std::shared_ptr<IpcConnection>> open;
        open.reserve(connections.size());
        for (const auto& [id, connection] : connections) open.push_back(connection);
        for (const auto& connection : open) connection->close(reason);
    }
};

IpcServer::IpcServer(IpcServerDeps deps, EngineHello hello) : impl_(std::make_unique<Impl>(deps, std::move(hello))) {
    impl_->op_events = deps.events.subscribe(EventFilter{{EventKind::OpCompleted}, {}, {}}, kSubscriptionQueueBytes);
    impl_->op_events->set_notify([impl = impl_.get()] { impl->on_op_events(); });
}

IpcServer::~IpcServer() {
    impl_->op_events->set_notify({});
    if (!impl_->stopped) stop(wire::GoodbyeReason::Shutdown);
    impl_->alive.reset();
}

Result<void> IpcServer::start(std::string_view endpoint) {
    impl_->stopped = false;
    Executor& strand = impl_->deps.strand;
    return impl_->deps.listener.listen(
        endpoint, [token = std::weak_ptr<char>(impl_->alive), impl = impl_.get(),
                   &strand](std::unique_ptr<ports::IByteStream> stream) mutable {
            strand.post([token, impl, stream = std::move(stream)]() mutable {
                if (token.expired()) {
                    stream->close();
                    return;
                }
                impl->accept(std::move(stream));
            });
        });
}

void IpcServer::stop(wire::GoodbyeReason reason) {
    impl_->stopped = true;
    impl_->deps.listener.close();
    impl_->close_all(reason);
}

void IpcServer::set_secrets_available(bool available) {
    if (impl_->hello.secrets_available == available) return;
    impl_->hello.secrets_available = available;
    for (const auto& [id, connection] : impl_->connections)
        if (connection->greeted()) connection->send(impl_->hello_ack(connection->info().compatibility));
}

void IpcServer::send_foreground_hint(u32 pid) {
    for (const auto& [id, connection] : impl_->connections)
        if (connection->greeted()) connection->send(wire::ForegroundHint{pid});
}

std::size_t IpcServer::connection_count() const noexcept { return impl_->connections.size(); }

}  // namespace reboot::ipc
