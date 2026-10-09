#include "reboot/ipc/ipc_client.hpp"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/ipc/ipc_errors.hpp"
#include "reboot/ipc/ipc_limits.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/ports/ipc.hpp"

namespace rb::ipc {

namespace {

namespace wire = contracts::ipc;

using Done = UniqueFunction<void(Result<Handshake>)>;

[[nodiscard]] Diagnostic connection_lost() {
    return make_diag(ErrorDomain::Ipc, kConnectionLost).kind(ErrorKind::EngineUnavailable).retryable().build();
}

[[nodiscard]] Diagnostic engine_closed(wire::GoodbyeReason reason) {
    return make_diag(ErrorDomain::Ipc, kEngineClosed)
        .arg("reason", reason)
        .kind(ErrorKind::EngineUnavailable)
        .retryable()
        .build();
}

[[nodiscard]] Diagnostic protocol_error(u64 frame_type) {
    return make_diag(ErrorDomain::Ipc, kProtocolError).arg("frame_type", frame_type).build();
}

// The frame type a codec error names, for ipc.protocol_error.
[[nodiscard]] u64 frame_type_of(const Diagnostic& codec_error) {
    for (const std::string_view name : {"frame_type", "actual"})
        if (const Arg* arg = codec_error.find_arg(name))
            if (const u64* type = std::get_if<u64>(arg)) return *type;
    return 0;
}

[[nodiscard]] std::optional<Diagnostic> refusal(ports::StartResult result) {
    const auto refused = [](MessageId id) {
        return make_diag(ErrorDomain::Ipc, id).kind(ErrorKind::EngineUnavailable).build();
    };
    switch (result) {
        case ports::StartResult::AwaitingUser: return refused(kAgentRequiresApproval);
        case ports::StartResult::CannotDetach: return refused(kEngineCannotDetach);
        case ports::StartResult::ElevatedRefused: return refused(kElevatedAutostartRefused);
        case ports::StartResult::NoInteractiveSession: return refused(kNoInteractiveSession);
        case ports::StartResult::Started:
        case ports::StartResult::AlreadyRunning:
        case ports::StartResult::ConnectOnly: break;
    }
    return std::nullopt;
}

// Retrying cannot change these, so a reconnect gives up on them.
[[nodiscard]] bool is_final(const Diagnostic& diag) {
    for (const MessageId* id : {&kRootMismatch, &kEndpointUntrusted, &kAgentRequiresApproval, &kEngineCannotDetach,
                                &kElevatedAutostartRefused, &kNoInteractiveSession})
        if (diag.is(*id)) return true;
    return false;
}

// Losses after which the same engine build will refuse this client again.
[[nodiscard]] bool may_reconnect_after(wire::GoodbyeReason reason) {
    return reason != wire::GoodbyeReason::ProtocolError && reason != wire::GoodbyeReason::VersionMismatch;
}

[[nodiscard]] bool same_path(const NativePath& a, const NativePath& b) {
    return a.lexically_normal() == b.lexically_normal();
}

}  // namespace

struct IpcClient::Impl {
    struct Core;
    std::shared_ptr<Core> core;
};

struct IpcClient::Impl::Core : std::enable_shared_from_this<Core> {
    enum class State : u8 { Idle, Connecting, Handshaking, Up, Failed, Closed };

    Core(IpcClientDeps client_deps, IpcClientOptions client_options)
        : deps(client_deps), options(std::move(client_options)) {}

    IpcClientDeps deps;
    const IpcClientOptions options;

    mutable std::mutex mutex;
    std::condition_variable idle;
    State state = State::Idle;
    bool closed = false;
    bool destroyed = false;
    // Sink, `done`, stream and executor calls running now; ~IpcClient waits for them.
    int active_calls = 0;

    std::shared_ptr<ports::IByteStream> link;
    // Bumped for every link and whenever one is dropped, so a stale stream callback is ignored.
    u64 generation = 0;
    // Bumped for every connect cycle, so a stale round is ignored.
    u64 cycle = 0;
    bool reconnecting = false;
    SteadyTime first_round_at{};
    SteadyTime deadline_at{};
    bool started_engine = false;
    bool marker_seen = false;
    std::optional<Diagnostic> last_error;
    std::chrono::milliseconds backoff = kReconnectBackoffMin;
    std::optional<Handshake> handshake;
    std::vector<Done> waiters;

    // Counts a call into the client from outside; a sink call never starts after close(), and
    // none starts once ~IpcClient runs, so the borrowed deps are not used after it.
    class CallScope {
    public:
        CallScope(Core& core, bool sink) : core_(core) {
            {
                const std::lock_guard lock(core.mutex);
                entered_ = !core.destroyed && !(sink && core.closed);
                if (entered_) ++core.active_calls;
            }
            if (!entered_) return;
            if (t_owner != &core) {
                saved_owner_ = t_owner;
                saved_depth_ = t_depth;
                swapped_ = true;
                t_owner = &core;
                t_depth = 0;
            }
            ++t_depth;
        }
        ~CallScope() {
            if (!entered_) return;
            --t_depth;
            if (swapped_) {
                t_owner = saved_owner_;
                t_depth = saved_depth_;
            }
            const std::lock_guard lock(core_.mutex);
            --core_.active_calls;
            core_.idle.notify_all();
        }
        CallScope(const CallScope&) = delete;
        CallScope& operator=(const CallScope&) = delete;

        explicit operator bool() const noexcept { return entered_; }

        // Calls this thread is inside of, so a destructor run from a sink call does not wait on itself.
        static int own_calls(const Core& core) noexcept { return t_owner == &core ? t_depth : 0; }

    private:
        static inline thread_local const Core* t_owner = nullptr;
        static inline thread_local int t_depth = 0;

        Core& core_;
        bool entered_ = false;
        bool swapped_ = false;
        const Core* saved_owner_ = nullptr;
        int saved_depth_ = 0;
    };

    void connect(Done done) {
        std::unique_lock lock(mutex);
        if (closed) {
            lock.unlock();
            post_done(std::move(done), std::unexpected(connection_lost()));
            return;
        }
        switch (state) {
            case State::Up: {
                Handshake current = *handshake;
                lock.unlock();
                post_done(std::move(done), std::move(current));
                return;
            }
            case State::Connecting:
            case State::Handshaking: {
                waiters.push_back(std::move(done));
                // A reconnect still waiting out its backoff starts now, so `done` keeps to connect_deadline.
                const SteadyTime now = deps.clock.steady_now();
                if (state != State::Connecting || first_round_at <= now) return;
                begin_cycle_locked(true, now);
                const u64 id = cycle;
                lock.unlock();
                schedule_round(id, now);
                return;
            }
            case State::Idle:
            case State::Failed:
            case State::Closed: break;
        }
        waiters.push_back(std::move(done));
        const SteadyTime now = deps.clock.steady_now();
        begin_cycle_locked(false, now);
        const u64 id = cycle;
        lock.unlock();
        schedule_round(id, now);
    }

    void begin_cycle_locked(bool reconnect, SteadyTime first_round) {
        ++cycle;
        state = State::Connecting;
        reconnecting = reconnect;
        first_round_at = first_round;
        deadline_at = first_round + options.connect_deadline;
        started_engine = false;
        marker_seen = false;
        last_error.reset();
    }

    // Runs `call` on the core unless the client is gone.
    template <class F>
    static void enter(const std::weak_ptr<Core>& weak, F&& call) {
        const std::shared_ptr<Core> core = weak.lock();
        if (!core) return;
        if (CallScope scope(*core, false); scope) call(*core);
    }

    void schedule_round(u64 id, SteadyTime when) {
        deps.executor.post_at(when, [weak = weak_from_this(), id] { enter(weak, [id](Core& core) { core.round(id); }); });
    }

    // One attempt: check the marker, connect, and start an engine when nobody listens.
    void round(u64 id) {
        bool start_allowed = false;
        {
            const std::lock_guard lock(mutex);
            if (closed || cycle != id || state != State::Connecting) return;
            start_allowed = options.launch_mode == LaunchMode::Autostart && !started_engine;
        }
        const bool marker = update_marker_fresh();
        const SteadyTime now = deps.clock.steady_now();
        SteadyTime deadline{};
        {
            const std::lock_guard lock(mutex);
            if (cycle != id) return;
            marker_seen = marker;
            deadline = deadline_at;
        }
        if (now >= deadline) {
            fail_cycle(id, timed_out());
            return;
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        Result<std::unique_ptr<ports::IByteStream>> stream =
            deps.connector.connect(options.endpoint, std::max(remaining, std::chrono::milliseconds{1}));
        if (stream) {
            begin_handshake(id, std::move(*stream));
            return;
        }
        if (stream.error().is(kEndpointUntrusted)) {
            fail_cycle(id, std::move(stream.error()));
            return;
        }
        {
            const std::lock_guard lock(mutex);
            if (cycle != id) return;
            last_error = std::move(stream.error());
        }

        // While an update replaces the engine, only its own restart may bring one up.
        if (!marker && start_allowed) {
            Result<ports::StartResult> started = deps.starter.ensure_started(options.engine_exe, options.data_root);
            if (!started) {
                fail_cycle(id, std::move(started.error()));
                return;
            }
            if (std::optional<Diagnostic> refused = refusal(*started)) {
                fail_cycle(id, std::move(*refused));
                return;
            }
            const std::lock_guard lock(mutex);
            if (cycle == id) started_engine = true;
        }
        schedule_round(id, std::min(deps.clock.steady_now() + kConnectPollInterval, deadline));
    }

    [[nodiscard]] SteadyTime deadline_at_of(u64 id) const {
        const std::lock_guard lock(mutex);
        return cycle == id ? deadline_at : SteadyTime::min();
    }

    [[nodiscard]] bool update_marker_fresh() {
        if (options.update_marker.empty()) return false;
        const Result<ports::FileRevision> marker = deps.files.revision(options.update_marker);
        if (!marker) return false;
        // Only a marker written within the bound counts; any other is left from a failed update.
        const auto now = deps.clock.system_now();
        return marker->mtime + kUpdateMarkerTimeout > now && marker->mtime < now + kUpdateMarkerTimeout;
    }

    [[nodiscard]] Diagnostic timed_out() const {
        const std::lock_guard lock(mutex);
        DiagBuilder diag = make_diag(ErrorDomain::Ipc, marker_seen ? kUpdateInProgress : kEngineUnavailable);
        std::move(diag).arg("deadline", options.connect_deadline).kind(ErrorKind::EngineUnavailable).retryable();
        if (last_error) std::move(diag).cause(*last_error);
        return std::move(diag).build();
    }

    // Ends the cycle; a reconnect that may still succeed backs off and runs another.
    void fail_cycle(u64 id, Diagnostic reason) {
        std::vector<Done> failed;
        bool final_loss = false;
        std::optional<SteadyTime> retry_at;
        {
            const std::lock_guard lock(mutex);
            if (closed || cycle != id) return;
            failed = std::move(waiters);
            waiters.clear();
            if (reconnecting && !is_final(reason)) {
                backoff = std::min(backoff * 2, kReconnectBackoffMax);
                retry_at = deps.clock.steady_now() + backoff;
                begin_cycle_locked(true, *retry_at);
                id = cycle;
            } else {
                final_loss = reconnecting;
                state = State::Failed;
            }
        }
        for (Done& done : failed) post_done(std::move(done), std::unexpected(reason));
        if (retry_at) {
            schedule_round(id, *retry_at);
            return;
        }
        if (!final_loss) return;
        if (CallScope scope(*this, true); scope) deps.sink.on_lost(reason, LinkLoss::Final);
    }

    void post_done(Done done, Result<Handshake> result) {
        deps.executor.post([weak = weak_from_this(), done = std::move(done), result = std::move(result)]() mutable {
            enter(weak, [&](Core&) { done(std::move(result)); });
        });
    }

    void begin_handshake(u64 id, std::unique_ptr<ports::IByteStream> stream) {
        std::shared_ptr<ports::IByteStream> opened(std::move(stream));
        u64 link_generation = 0;
        SteadyTime deadline{};
        {
            const std::lock_guard lock(mutex);
            if (closed || cycle != id || state != State::Connecting) {
                retire(std::move(opened));
                return;
            }
            link_generation = ++generation;
            link = opened;
            state = State::Handshaking;
            deadline = deadline_at;
        }
        const std::weak_ptr<Core> weak = weak_from_this();
        opened->on_read([weak, link_generation, codec = std::make_unique<IpcCodec>()](std::span<const u8> bytes) {
            enter(weak, [&](Core& core) { core.on_bytes(link_generation, *codec, bytes); });
        });
        opened->on_close([weak, link_generation] {
            enter(weak, [link_generation](Core& core) { core.on_link_closed(link_generation); });
        });
        opened->write(IpcCodec::encode(options.hello));
        deps.executor.post_at(deadline, [weak, link_generation] {
            enter(weak, [link_generation](Core& core) { core.handshake_timeout(link_generation); });
        });
    }

    // Never destroys the stream here: this may run on its own reader thread.
    void retire(std::shared_ptr<ports::IByteStream> stream) {
        if (!stream) return;
        stream->close();
        deps.executor.post([stream = std::move(stream)] {});
    }

    [[nodiscard]] std::shared_ptr<ports::IByteStream> drop_link_locked() {
        ++generation;
        return std::move(link);
    }

    void handshake_timeout(u64 link_generation) {
        std::shared_ptr<ports::IByteStream> dropped;
        u64 id = 0;
        {
            const std::lock_guard lock(mutex);
            if (link_generation != generation || state != State::Handshaking) return;
            dropped = drop_link_locked();
            state = State::Connecting;
            id = cycle;
        }
        retire(std::move(dropped));
        fail_cycle(id, timed_out());
    }

    void on_bytes(u64 link_generation, IpcCodec& codec, std::span<const u8> bytes) {
        Result<std::vector<EngineMessage>> decoded = codec.feed_from_engine(bytes);
        if (!decoded) {
            on_link_error(link_generation, protocol_error(frame_type_of(decoded.error())), true);
            return;
        }
        for (EngineMessage& message : *decoded)
            if (!on_message(link_generation, std::move(message))) return;
    }

    // False once the link is gone, so the rest of the read is dropped.
    bool on_message(u64 link_generation, EngineMessage message) {
        std::unique_lock lock(mutex);
        if (link_generation != generation) return false;
        if (state == State::Handshaking) {
            lock.unlock();
            return on_handshake_message(link_generation, std::move(message));
        }
        if (state != State::Up) return false;
        if (const auto* goodbye = std::get_if<wire::Goodbye>(&message)) {
            std::shared_ptr<ports::IByteStream> dropped = drop_link_locked();
            lock.unlock();
            retire(std::move(dropped));
            lose(engine_closed(goodbye->reason), may_reconnect_after(goodbye->reason));
            return false;
        }
        if (const auto* ack = std::get_if<wire::HelloAck>(&message)) handshake->hello = *ack;
        lock.unlock();
        CallScope scope(*this, true);
        if (!scope) return false;
        deps.sink.on_frame(std::move(message));
        return true;
    }

    bool on_handshake_message(u64 link_generation, EngineMessage message) {
        if (std::holds_alternative<wire::Goodbye>(message)) {
            // The engine is going away; the next round finds its successor or starts one.
            on_link_error(link_generation, connection_lost(), false);
            return false;
        }
        auto* ack = std::get_if<wire::HelloAck>(&message);
        if (ack == nullptr) {
            const u64 type = std::visit([](const auto& m) { return contract_frame_type_v<std::decay_t<decltype(m)>>; },
                                        message);
            on_link_error(link_generation, protocol_error(type), true);
            return false;
        }

        const Result<NativePath> engine_root = from_wire(ack->canonical_root);
        if (!engine_root || !same_path(*engine_root, options.canonical_root)) {
            on_link_error(link_generation,
                          make_diag(ErrorDomain::Ipc, kRootMismatch)
                              .arg("engine_root", ack->canonical_root)
                              .arg("root", options.canonical_root)
                              .kind(ErrorKind::Conflict)
                              .build(),
                          true);
            return false;
        }
        Handshake result{*ack, std::nullopt};
        if (!options.engine_exe.empty()) {
            const Result<NativePath> image = from_wire(ack->image_path);
            if (!image || !same_path(*image, options.engine_exe))
                result.image_warning = make_diag(ErrorDomain::Ipc, kEngineImageDiffers)
                                           .arg("image_path", ack->image_path)
                                           .arg("expected_path", options.engine_exe)
                                           .severity(Severity::Warning)
                                           .build();
        }

        std::vector<Done> succeeded;
        bool reconnected = false;
        {
            const std::lock_guard lock(mutex);
            if (link_generation != generation || state != State::Handshaking) return false;
            state = State::Up;
            handshake = result;
            backoff = kReconnectBackoffMin;
            reconnected = reconnecting;
            succeeded = std::move(waiters);
            waiters.clear();
        }
        // Posted first: the sink may close or destroy the client, after which deps are not used.
        for (Done& done : succeeded) post_done(std::move(done), result);
        if (reconnected) {
            if (CallScope scope(*this, true); scope) deps.sink.on_reconnected(result);
        }
        return true;
    }

    // A broken or refused link: during the handshake it ends the round, once up it is a loss.
    void on_link_error(u64 link_generation, Diagnostic reason, bool fatal_to_round) {
        std::shared_ptr<ports::IByteStream> dropped;
        State was{};
        u64 id = 0;
        {
            const std::lock_guard lock(mutex);
            if (link_generation != generation) return;
            dropped = drop_link_locked();
            was = state;
            id = cycle;
            if (was == State::Handshaking) state = State::Connecting;
        }
        if (was == State::Up && reason.is(kProtocolError))
            dropped->write(IpcCodec::encode(wire::Goodbye{wire::GoodbyeReason::ProtocolError}));
        retire(std::move(dropped));
        if (was == State::Handshaking) {
            if (fatal_to_round) fail_cycle(id, std::move(reason));
            else schedule_round(id, std::min(deps.clock.steady_now() + kConnectPollInterval, deadline_at_of(id)));
        } else if (was == State::Up) {
            lose(std::move(reason), true);
        }
    }

    void on_link_closed(u64 link_generation) { on_link_error(link_generation, connection_lost(), false); }

    // The link that was up is gone; Autostart backs off and reconnects.
    void lose(Diagnostic reason, bool may_reconnect) {
        bool final_loss = false;
        u64 id = 0;
        SteadyTime retry_at{};
        {
            const std::lock_guard lock(mutex);
            if (closed) return;
            handshake.reset();
            final_loss = !may_reconnect || options.launch_mode != LaunchMode::Autostart;
            if (final_loss) {
                state = State::Failed;
            } else {
                backoff = kReconnectBackoffMin;
                retry_at = deps.clock.steady_now() + backoff;
                begin_cycle_locked(true, retry_at);
                id = cycle;
            }
        }
        if (CallScope scope(*this, true); scope)
            deps.sink.on_lost(reason, final_loss ? LinkLoss::Final : LinkLoss::Reconnecting);
        if (final_loss) return;
        {
            // The sink may have closed or destroyed the client.
            const std::lock_guard lock(mutex);
            if (closed || cycle != id) return;
        }
        schedule_round(id, retry_at);
    }

    Result<void> write(std::span<const u8> frame) {
        // The engine would end the link over a frame it cannot take.
        if (!IpcCodec::fits_frame_cap(frame))
            return make_diag(ErrorDomain::Ipc, kMessageTooLarge)
                .arg("size", frame.size())
                .arg("limit", kIpcFrameCap)
                .kind(ErrorKind::InvalidInput)
                .fail();
        std::shared_ptr<ports::IByteStream> current;
        {
            const std::lock_guard lock(mutex);
            if (state != State::Up || !link) return std::unexpected(connection_lost());
            current = link;
        }
        current->write(frame);
        return {};
    }

    void close() {
        std::shared_ptr<ports::IByteStream> dropped;
        std::vector<Done> pending;
        {
            const std::lock_guard lock(mutex);
            if (closed) return;
            closed = true;
            state = State::Closed;
            ++cycle;
            dropped = drop_link_locked();
            pending = std::move(waiters);
            waiters.clear();
            handshake.reset();
        }
        if (dropped) dropped->write(IpcCodec::encode(wire::Goodbye{wire::GoodbyeReason::Normal}));
        retire(std::move(dropped));
        for (Done& done : pending) post_done(std::move(done), std::unexpected(connection_lost()));
    }

    void wait_for_calls() {
        std::unique_lock lock(mutex);
        destroyed = true;
        const int own = CallScope::own_calls(*this);
        idle.wait(lock, [&] { return active_calls <= own; });
    }
};

IpcClient::IpcClient(IpcClientDeps deps, IpcClientOptions options)
    : impl_(std::make_unique<Impl>(Impl{std::make_shared<Impl::Core>(deps, std::move(options))})) {}

IpcClient::~IpcClient() {
    impl_->core->close();
    impl_->core->wait_for_calls();
}

void IpcClient::connect(UniqueFunction<void(Result<Handshake>)> done) { impl_->core->connect(std::move(done)); }

void IpcClient::close() { impl_->core->close(); }

Result<void> IpcClient::send_secret_put(std::span<const u8> target, const SecretBytes& secret) {
    const SecretBytes frame = IpcCodec::encode_secret_put(target, secret);
    return impl_->core->write(frame.reveal());
}

Result<void> IpcClient::write(wire::Bytes frame) { return impl_->core->write(frame); }

}  // namespace rb::ipc
