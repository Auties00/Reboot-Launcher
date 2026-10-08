#include "client_context.hpp"

#include <utility>
#include <variant>

#include "api_event_kind.hpp"
#include "api_event_payload.hpp"
#include "api_outcome.hpp"
#include "engine_executable.hpp"
#include "messages.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/ipc/compatibility.hpp"
#include "reboot/ipc/endpoint.hpp"
#include "reboot/ipc/ipc_client.hpp"
#include "reboot/ipc/ipc_errors.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/ports/platform_paths.hpp"

namespace reboot::client {

namespace {

namespace wire = contracts::ipc;

template <class... F>
struct Overloaded : F... {
    using F::operator()...;
};

[[nodiscard]] Diagnostic closed_diag() { return make_diag(ErrorDomain::Client, msg::kClosed).kind(ErrorKind::EngineUnavailable); }

[[nodiscard]] Diagnostic connection_lost_diag() {
    return make_diag(ErrorDomain::Ipc, ipc::kConnectionLost).kind(ErrorKind::EngineUnavailable).retryable();
}

[[nodiscard]] Diagnostic unknown_op(u64 op_id) {
    return make_diag(ErrorDomain::Ipc, ipc::kUnknownOp).arg("op", op_id).kind(ErrorKind::NotFound);
}

[[nodiscard]] Diagnostic unknown_subscription(u64 sub_id) {
    return make_diag(ErrorDomain::Ipc, ipc::kUnknownSubscription).arg("sub", sub_id).kind(ErrorKind::NotFound);
}

template <class Frame>
[[nodiscard]] Diagnostic unexpected_answer() {
    return make_diag(ErrorDomain::Ipc, ipc::kProtocolError).arg("frame_type", contract_frame_type_v<Frame>);
}

// The Reply a Call or SecretReveal expects; a Started there is a protocol error.
[[nodiscard]] CallResult<wire::Bytes> reply_payload(Result<Answer> answer) {
    if (!answer) return std::unexpected(local_failure(answer.error()));
    auto* reply = std::get_if<wire::Reply>(&*answer);
    if (reply == nullptr) return std::unexpected(local_failure(unexpected_answer<wire::Started>()));
    if (reply->error) return std::unexpected(CallFailure{std::move(*reply->error), true});
    return std::move(reply->payload).value_or(wire::Bytes{});
}

}  // namespace

Result<std::unique_ptr<ClientContext>> ClientContext::create(ClientDeps deps, ConnectSettings settings) {
    Result<DataRoot> root = settings.data_root
                                ? Result<DataRoot>{DataRoot{*settings.data_root, true}}
                                : resolve_data_root(deps.paths, deps.launcher_home ? std::optional<std::string_view>{
                                                                                         *deps.launcher_home}
                                                                                   : std::nullopt);
    if (!root) return std::unexpected(std::move(root.error()));
    const NativePath canonical = canonical_root(*root);
    auto endpoint = ipc::endpoint_for(deps.self, root_hash16(canonical));
    if (!endpoint) return std::unexpected(std::move(endpoint.error()));

    ipc::IpcClientOptions options{
        .endpoint = std::move(*endpoint),
        .engine_exe = engine_executable(deps.paths),
        .data_root = *root,
        .canonical_root = canonical,
        .update_marker = AppLayout{*root, deps.paths}.update_marker(),
        .hello = wire::Hello{settings.client_kind, REBOOT_BUILD_ID, u32{RB_ABI_MAJOR} << 16 | u32{RB_ABI_MINOR},
                             deps.self.pid, to_wire(deps.caller.capture())},
        .launch_mode = settings.launch_mode,
        .connect_deadline = settings.connect_deadline,
    };

    std::unique_ptr<ClientContext> context{new ClientContext(std::move(deps))};
    ipc::IpcClientDeps link_deps{context->deps_.connector, context->deps_.starter, context->deps_.files,
                                 context->deps_.clock,     context->deps_.executor, *context};
    context->link_ = std::make_unique<ipc::IpcClient>(link_deps, std::move(options));
    return context;
}

ClientContext::ClientContext(ClientDeps deps) : deps_(std::move(deps)) {}

ClientContext::~ClientContext() {
    close();
    // The IpcClient waits for a sink call in progress, which still uses the members below it.
    link_.reset();
}

void ClientContext::connect(UniqueFunction<void(Result<Connected>)> done) {
    link_->connect([this, done = std::move(done)](Result<ipc::Handshake> handshake) mutable {
        if (!handshake) {
            done(std::unexpected(std::move(handshake.error())));
            return;
        }
        {
            std::lock_guard lock(link_mutex_);
            hello_ = handshake->hello;
            link_generation_ = 1;
        }
        deps_.caller.allow_foreground(handshake->hello.pid);
        done(Connected{std::move(handshake->image_warning)});
    });
}

void ClientContext::close() {
    {
        std::lock_guard lock(link_mutex_);
        closed_ = true;
    }
    if (link_) link_->close();
    calls_.fail_all(closed_diag());
    std::lock_guard lock(subs_mutex_);
    for (auto& [sub_id, sub] : subs_) sub->close();
}

void ClientContext::call(u32 method, std::span<const u8> request, std::chrono::milliseconds timeout,
                         UniqueFunction<void(CallResult<std::vector<u8>>)> done) {
    if (auto allowed = check_method(method); !allowed) {
        done(std::unexpected(local_failure(allowed.error())));
        return;
    }
    wire::Call frame{0, method, wire::Bytes(request.begin(), request.end()), static_cast<u32>(timeout.count())};
    const std::optional<u64> req_id = send_call(
        std::move(frame), [done = std::move(done)](Result<Answer> answer) mutable { done(reply_payload(std::move(answer))); });
    if (!req_id || timeout.count() == 0) return;
    deps_.executor.post_at(deps_.clock.steady_now() + timeout, [this, id = *req_id, method, timeout] {
        calls_.fail(id, make_diag(ErrorDomain::Client, msg::kCallTimedOut)
                            .arg("method", method)
                            .arg("timeout", timeout)
                            .retryable()
                            .build());
    });
}

void ClientContext::start(u32 method, std::span<const u8> request, std::optional<bool> detached,
                          UniqueFunction<void(CallResult<u64>)> done) {
    if (auto allowed = check_method(method); !allowed) {
        done(std::unexpected(local_failure(allowed.error())));
        return;
    }
    wire::Start frame{0, method, wire::Bytes(request.begin(), request.end()), detached};
    send_call(std::move(frame), [this, method, done = std::move(done)](Result<Answer> answer) mutable {
        if (!answer) return done(std::unexpected(local_failure(answer.error())));
        if (auto* started = std::get_if<wire::Started>(&*answer)) {
            // On the reader thread, before any OpResult for the op is handled.
            track_op(started->op_id, method);
            return done(started->op_id);
        }
        auto& reply = std::get<wire::Reply>(*answer);
        if (reply.error) return done(std::unexpected(CallFailure{std::move(*reply.error), true}));
        done(std::unexpected(local_failure(unexpected_answer<wire::Reply>())));
    });
}

void ClientContext::reveal_secret(std::span<const u8> target, UniqueFunction<void(CallResult<SecretBytes>)> done) {
    wire::SecretReveal frame{0, wire::Bytes(target.begin(), target.end())};
    send_call(std::move(frame), [done = std::move(done)](Result<Answer> answer) mutable {
        auto payload = reply_payload(std::move(answer));
        if (!payload) return done(std::unexpected(std::move(payload.error())));
        done(SecretBytes{std::move(*payload)});
    });
}

Result<void> ClientContext::attach(u64 op_id) {
    {
        std::lock_guard lock(link_mutex_);
        if (closed_) return std::unexpected(closed_diag());
    }
    track_op(op_id, 0);
    if (auto sent = link_->send(wire::Attach{op_id}); !sent) {
        static_cast<void>(release(op_id));
        return sent;
    }
    return {};
}

Result<void> ClientContext::cancel(u64 op_id) {
    if (std::holds_alternative<OpUnknown>(ops_.state(op_id))) return std::unexpected(unknown_op(op_id));
    return link_->send(wire::Cancel{op_id});
}

Result<OpState> ClientContext::op_state(u64 op_id) const {
    OpState state = ops_.state(op_id);
    if (std::holds_alternative<OpUnknown>(state)) return std::unexpected(unknown_op(op_id));
    return state;
}

Result<void> ClientContext::release(u64 op_id) {
    {
        std::lock_guard lock(subs_mutex_);
        if (!ops_.release(op_id)) return std::unexpected(unknown_op(op_id));
        for (auto& [sub_id, sub] : subs_) sub->unfollow(op_id);
    }
    // A lost link drops the connection's handles in the engine, so a failed write needs nothing.
    static_cast<void>(link_->send(wire::Release{op_id}));
    return {};
}

Result<u64> ClientContext::subscribe(std::span<const u8> filter) {
    auto decoded = decode_event_filter(filter);
    if (!decoded) return std::unexpected(std::move(decoded.error()));
    std::lock_guard lock(subs_mutex_);
    u64 link = 0;
    {
        std::lock_guard link_lock(link_mutex_);
        if (closed_) return std::unexpected(closed_diag());
        link = link_generation_;
    }
    if (subs_.size() >= wire::kMaxSubscriptions)
        return make_diag(ErrorDomain::Ipc, ipc::kTooManySubscriptions)
            .arg("limit", wire::kMaxSubscriptions)
            .kind(ErrorKind::Conflict)
            .fail();
    const u64 sub_id = next_sub_id_++;
    wire::Bytes bytes(filter.begin(), filter.end());
    auto sub = std::make_unique<EventSubscription>(sub_id, bytes, std::move(*decoded), link);
    for (const PendingOp& op : ops_.pending()) sub->follow(op.op_id);
    // In the map before Subscribe goes out, so the first EventBatch finds it.
    subs_.emplace(sub_id, std::move(sub));
    if (auto sent = link_->send(wire::Subscribe{sub_id, std::move(bytes)}); !sent) {
        subs_.erase(sub_id);
        return std::unexpected(std::move(sent.error()));
    }
    return sub_id;
}

Result<void> ClientContext::unsubscribe(u64 sub_id) {
    std::unique_ptr<EventSubscription> sub;
    {
        std::lock_guard lock(subs_mutex_);
        const auto it = subs_.find(sub_id);
        if (it == subs_.end()) return std::unexpected(unknown_subscription(sub_id));
        sub = std::move(it->second);
        subs_.erase(it);
    }
    sub->close();
    sub->wait_unused();
    static_cast<void>(link_->send(wire::Unsubscribe{sub_id}));
    return {};
}

Result<std::optional<wire::WireEvent>> ClientContext::next_event(u64 sub_id,
                                                                 std::optional<std::chrono::milliseconds> wait) {
    EventSubscription* sub = acquire(sub_id);
    if (sub == nullptr) return std::unexpected(unknown_subscription(sub_id));
    TakenEvent taken = sub->take();
    if (!taken.event && !taken.closed && (!wait || wait->count() > 0)) {
        const u64 ticket = sub->open_wait();
        if (wait)
            deps_.executor.post_at(deps_.clock.steady_now() + *wait, [this, sub_id, ticket] {
                if (EventSubscription* waiting = acquire(sub_id)) {
                    waiting->expire_wait(ticket);
                    waiting->release();
                }
            });
        taken = sub->take_waiting(ticket);
    }
    sub->release();
    send_credit(sub_id, taken.credit);
    if (taken.event) return std::move(taken.event);
    if (taken.closed) return std::unexpected(closed_diag());
    return std::nullopt;
}

void ClientContext::set_wake(u64 sub_id, WakeCallback wake) {
    EventSubscription* sub = acquire(sub_id);
    if (sub == nullptr) return;
    sub->set_wake(wake);
    sub->release();
    if (wake.fn == nullptr) return;
    // Never on the caller's thread: the wake may call back into the library.
    deps_.executor.post([this, sub_id] {
        if (EventSubscription* armed = acquire(sub_id)) {
            apply(sub_id, PushEffects{armed->arm_wake(), 0});
            armed->release();
        }
    });
}

Result<void> ClientContext::put_secret(std::span<const u8> target, std::span<const u8> secret) {
    return link_->send_secret_put(target, SecretBytes{std::vector<u8>(secret.begin(), secret.end())});
}

void ClientContext::log_write(LogLevel level, std::string_view utf8) {
    static_cast<void>(link_->send(wire::LogWrite{level, std::string{utf8}}));
}

void ClientContext::on_frame(ipc::EngineMessage message) {
    std::visit(Overloaded{
                   [this](wire::HelloAck& ack) {
                       std::lock_guard lock(link_mutex_);
                       hello_ = std::move(ack);
                   },
                   [this](wire::Reply& reply) { calls_.answer(reply.req_id, std::move(reply)); },
                   [this](wire::Started& started) { calls_.answer(started.req_id, started); },
                   [this](wire::OpResult& result) { deliver_outcome(result.op_id, std::move(result.outcome)); },
                   [this](wire::EventBatch& batch) { on_event_batch(std::move(batch)); },
                   [this](wire::Resync& resync) { on_resync(resync.sub_id); },
                   [this](wire::ForegroundHint& hint) { deps_.caller.allow_foreground(hint.pid); },
                   // IpcClient reports a Goodbye through on_lost.
                   [](auto&) {},
               },
               message);
}

void ClientContext::on_lost(const Diagnostic& reason, ipc::LinkLoss loss) {
    u64 epoch = 0;
    {
        std::lock_guard lock(link_mutex_);
        lost_reason_ = reason;
        epoch = hello_.epoch;
    }
    calls_.fail_all(reason);
    if (loss == ipc::LinkLoss::Final) fail_pending_ops(reason);
    push_local_to_all(library_event(ApiEventKind::ConnectionLost, epoch));
    if (loss != ipc::LinkLoss::Final) return;
    {
        std::lock_guard lock(link_mutex_);
        closed_ = true;
    }
    std::lock_guard lock(subs_mutex_);
    for (auto& [sub_id, sub] : subs_) sub->close();
}

void ClientContext::on_reconnected(const ipc::Handshake& handshake) {
    bool new_epoch = false;
    u64 link = 0;
    Diagnostic reason = connection_lost_diag();
    {
        std::lock_guard lock(link_mutex_);
        new_epoch = handshake.hello.epoch != hello_.epoch;
        hello_ = handshake.hello;
        link = ++link_generation_;
        if (lost_reason_) reason = std::move(*lost_reason_);
        lost_reason_.reset();
    }
    deps_.caller.allow_foreground(handshake.hello.pid);
    {
        std::lock_guard lock(subs_mutex_);
        for (auto& [sub_id, sub] : subs_) sub->set_link(link);
    }
    push_local_to_all(library_event(ApiEventKind::Reconnected, handshake.hello.epoch));
    if (new_epoch) {
        fail_pending_ops(reason);
        std::lock_guard lock(subs_mutex_);
        for (auto& [sub_id, sub] : subs_) sub->forget_delivered();
    } else {
        // The engine replays the OpResult of an op that ended meanwhile.
        for (const PendingOp& op : ops_.pending()) static_cast<void>(link_->send(wire::Attach{op.op_id}));
    }
    resubscribe_all();
    // Events published while the link was down are gone.
    push_local_to_all(library_event(ApiEventKind::Resync, handshake.hello.epoch));
}

Result<void> ClientContext::check_method(u32 method) const {
    std::lock_guard lock(link_mutex_);
    if (closed_) return std::unexpected(closed_diag());
    if (!ipc::allows_method(hello_.compatibility, method))
        return make_diag(ErrorDomain::Ipc, ipc::kVersionMismatch)
            .arg("method", method)
            .arg("engine_build", hello_.engine_build)
            .kind(ErrorKind::Unsupported)
            .fail();
    return {};
}

template <class Frame>
std::optional<u64> ClientContext::send_call(Frame frame, AnswerDone done) {
    const std::optional<u64> req_id = calls_.open(std::move(done));
    if (!req_id) return std::nullopt;
    frame.req_id = *req_id;
    if (auto sent = link_->send(frame); !sent) calls_.fail(*req_id, sent.error());
    return req_id;
}

void ClientContext::track_op(u64 op_id, u32 method_id) {
    std::lock_guard lock(subs_mutex_);
    ops_.track(op_id, method_id);
    for (auto& [sub_id, sub] : subs_) sub->follow(op_id);
}

void ClientContext::on_event_batch(wire::EventBatch batch) {
    EventSubscription* sub = acquire(batch.sub_id);
    if (sub == nullptr) return;
    u64 link = 0;
    {
        std::lock_guard lock(link_mutex_);
        link = link_generation_;
    }
    apply(batch.sub_id, sub->push_from_engine(std::move(batch.events), link));
    sub->release();
}

void ClientContext::on_resync(u64 sub_id) {
    EventSubscription* sub = acquire(sub_id);
    if (sub == nullptr) return;
    u64 epoch = 0;
    {
        std::lock_guard lock(link_mutex_);
        epoch = hello_.epoch;
    }
    apply(sub_id, sub->push_local(library_event(ApiEventKind::Resync, epoch)));
    sub->release();
}

void ClientContext::deliver_outcome(u64 op_id, std::vector<u8> outcome) {
    u64 epoch = 0;
    {
        std::lock_guard lock(link_mutex_);
        epoch = hello_.epoch;
    }
    const wire::WireEvent event = op_completed_event(epoch, op_id, outcome);
    std::vector<std::pair<EventSubscription*, PushEffects>> woken;
    {
        // Under subs_mutex_, so a subscription made meanwhile either follows the op or sees it ended.
        std::lock_guard lock(subs_mutex_);
        if (!ops_.complete(op_id, std::move(outcome))) return;
        for (auto& [sub_id, sub] : subs_) {
            PushEffects effects = sub->push_outcome(op_id, event);
            if (!effects.wake) continue;
            sub->acquire();
            woken.emplace_back(sub.get(), effects);
        }
    }
    for (auto& [sub, effects] : woken) {
        apply(sub->id(), effects);
        sub->release();
    }
}

void ClientContext::fail_pending_ops(const Diagnostic& reason) {
    for (const PendingOp& op : ops_.pending())
        deliver_outcome(op.op_id, encode_failed_outcome(op.op_id, op.method_id, reason));
}

void ClientContext::push_local_to_all(wire::WireEvent event) {
    std::vector<std::pair<EventSubscription*, PushEffects>> woken;
    {
        std::lock_guard lock(subs_mutex_);
        for (auto& [sub_id, sub] : subs_) {
            PushEffects effects = sub->push_local(event);
            if (!effects.wake) continue;
            sub->acquire();
            woken.emplace_back(sub.get(), effects);
        }
    }
    for (auto& [sub, effects] : woken) {
        apply(sub->id(), effects);
        sub->release();
    }
}

void ClientContext::resubscribe_all() {
    std::lock_guard lock(subs_mutex_);
    for (auto& [sub_id, sub] : subs_) static_cast<void>(link_->send(wire::Subscribe{sub_id, sub->filter()}));
}

void ClientContext::apply(u64 sub_id, const PushEffects& effects) {
    if (effects.wake) effects.wake->fn(effects.wake->user);
    send_credit(sub_id, effects.credit);
}

void ClientContext::send_credit(u64 sub_id, u32 n) {
    if (n == 0) return;
    // A lost link restarts the window at Subscribe, so a failed write needs nothing.
    static_cast<void>(link_->send(wire::Credit{sub_id, n}));
}

EventSubscription* ClientContext::acquire(u64 sub_id) const {
    std::lock_guard lock(subs_mutex_);
    const auto it = subs_.find(sub_id);
    if (it == subs_.end()) return nullptr;
    it->second->acquire();
    return it->second.get();
}

wire::CallerContext ClientContext::to_wire(const ports::CallerContext& caller) {
    wire::CallerContext out{caller.os_session, caller.elevated, {}};
    out.display_env.reserve(caller.display_env.size());
    for (const auto& [name, value] : caller.display_env) out.display_env.push_back(wire::EnvVar{name, value});
    return out;
}

}  // namespace reboot::client
