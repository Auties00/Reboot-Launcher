#include "reboot/process/child_supervisor.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>

#include "messages.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/flat_map.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/process/child_channel.hpp"
#include "reboot/process/child_observer.hpp"
#include "reboot/process/child_request_handler.hpp"
#include "reboot/process/line_reader.hpp"

namespace reboot::process {

namespace {

namespace common = contracts::common;

// Field 1 of every correlated contract message; decoding skips the rest.
struct CorrelationHeader {
    u64 req_id = 0;
};

[[nodiscard]] std::optional<u64> read_req_id(std::span<const u8> payload) {
    CorrelationHeader header;
    if (!sb::wire::decode(payload, header)) return std::nullopt;
    return header.req_id;
}

[[nodiscard]] LogLevel clamp_level(LogLevel level) { return level > LogLevel::Error ? LogLevel::Error : level; }

}  // namespace

struct ChildSupervisor::Impl {
    struct PendingRequest {
        u64 request_type = 0;
        std::optional<u64> reply_type;
        UniqueFunction<void(Result<ReplyFrame>)> done;
    };

    ChildSupervisor& owner;
    ports::IProcessLauncher& launcher;
    Executor& strand;
    TimerService& timers;
    const IClock& clock;
    ProcessSpec spec;
    ChildHandshake handshake;
    ChildSupervisorOptions options;
    ChildRequestHandler* requests;
    ChildObserver& observer;
    ChildRecordCallback record;
    std::string program = display_utf8(spec.exe.filename());

    ChildState state = ChildState::Idle;
    u32 generation = 0;
    u64 last_req_id = 0;
    u64 ping_nonce = 0;
    u32 missed_pings = 0;
    bool ping_outstanding = false;

    std::unique_ptr<ports::ChildProcess> child;
    std::unique_ptr<ChildChannel> channel;
    std::unique_ptr<LineReader> stderr_lines;
    std::optional<ChildRecord> spawned;
    // Cancelled when the generation ends; posted I/O callbacks check it before touching anything.
    CancelSource generation_alive;
    // Cancelled by the destructor, so a posted not-running answer is dropped with the supervisor.
    CancelSource supervisor_alive;
    // Set once the child is being ended on purpose; its exit reports this cause.
    std::optional<ChildExitCause> ending;
    std::optional<Diagnostic> ending_error;

    TimerHandle hello_timer;
    TimerHandle ping_timer;
    TimerHandle stop_timer;
    TimerHandle restart_timer;
    std::vector<SteadyTime> restarts;

    FlatMap<u64, PendingRequest> pending;
    std::vector<ChildReply*> replies;

    [[nodiscard]] bool open() const { return channel && channel->phase() == ChannelPhase::Open && !ending; }

    Result<void> spawn() {
        if (spec.stdio != ports::StdioMode::ControlChannel)
            return make_diag(ErrorDomain::Process, msg::kSpecNeedsControlChannel)
                .arg("program", program)
                .kind(ErrorKind::InvalidInput)
                .fail();
        if (Result<void> valid = spec.validate(); !valid) return valid;

        ++generation;
        Result<std::unique_ptr<ports::ChildProcess>> started = [&] {
            const WipingLaunch launch = spec.to_launch();
            return launcher.spawn(launch.get());
        }();
        if (!started) return std::unexpected(std::move(started.error()));

        child = std::move(*started);
        generation_alive = CancelSource{};
        ending.reset();
        ending_error.reset();
        missed_pings = 0;
        ping_outstanding = false;
        state = ChildState::Starting;

        ports::ChildProcess* process = child.get();
        channel = std::make_unique<ChildChannel>(
            program, handshake, [process](std::span<const u8> bytes) { process->write_stdin(bytes); },
            [this](const RawFrame& frame) { on_frame(frame); });
        stderr_lines = std::make_unique<LineReader>([this](std::string_view line, bool) {
            if (Logger::enabled(options.stderr_level))
                Logger::write(options.stderr_level, options.log_category, spec.session, std::string(line));
        });

        const CancelToken alive = generation_alive.token();
        Executor* target = &strand;
        child->on_stdout([this, alive, target](std::span<const u8> bytes) {
            target->post([this, alive, data = std::vector<u8>(bytes.begin(), bytes.end())] {
                if (!alive.cancelled()) on_stdout(data);
            });
        });
        child->on_stderr([this, alive, target](std::span<const u8> bytes) {
            target->post([this, alive, data = std::vector<u8>(bytes.begin(), bytes.end())] {
                if (!alive.cancelled() && stderr_lines) stderr_lines->feed(data);
            });
        });
        child->on_exit([this, alive, target](ports::ChildExit status) {
            target->post([this, alive, status] {
                if (!alive.cancelled()) on_child_exit(status);
            });
        });

        hello_timer = timers.after(options.hello_timeout, [this] {
            if (state != ChildState::Starting || ending) return;
            end_child(ChildExitCause::HandshakeFailed, make_diag(ErrorDomain::Process, msg::kChildHelloTimeout)
                                                           .arg("program", program)
                                                           .arg("timeout", options.hello_timeout)
                                                           .build());
        });

        spawned = ChildRecord{.pid = child->pid(), .created = child->created(), .role = spec.role, .session = spec.session};
        if (record) record(*spawned, RecordChange::Spawned);
        return {};
    }

    void on_stdout(std::span<const u8> bytes) {
        if (!channel || ending) return;
        const bool awaiting_hello = channel->phase() == ChannelPhase::AwaitingHello;
        Result<void> fed = channel->feed(bytes);
        if (!fed) {
            if (!ending)
                end_child(awaiting_hello && state == ChildState::Starting ? ChildExitCause::HandshakeFailed
                                                                          : ChildExitCause::ProtocolError,
                          std::move(fed.error()));
            return;
        }
        if (state == ChildState::Starting && open()) mark_running();
    }

    void mark_running() {
        hello_timer.cancel();
        state = ChildState::Running;
        schedule_ping();
        observer.on_running(generation);
    }

    void schedule_ping() {
        ping_timer = timers.after(options.liveness.interval, [this] { on_ping_tick(); });
    }

    void on_ping_tick() {
        if (state != ChildState::Running || !open()) return;
        if (ping_outstanding && ++missed_pings >= options.liveness.miss_limit) {
            const u32 missed = missed_pings;
            missed_pings = 0;
            observer.on_unresponsive(missed);
            if (options.restart && !ending) {
                end_child(ChildExitCause::Unresponsive, make_diag(ErrorDomain::Process, msg::kChildUnresponsive)
                                                            .arg("program", program)
                                                            .arg("missed", missed)
                                                            .build());
                return;
            }
        }
        if (!open()) return;
        channel->send(common::Ping{++ping_nonce});
        ping_outstanding = true;
        schedule_ping();
    }

    void on_frame(const RawFrame& frame) {
        if (ending) return;
        if (state == ChildState::Starting) mark_running();

        if (is_frame<common::Pong>(frame)) {
            ping_outstanding = false;
            missed_pings = 0;
            return;
        }
        if (is_frame<common::Log>(frame)) {
            Result<common::Log> log = decode_contract<common::Log>(frame.payload);
            if (!log) return end_child(ChildExitCause::ProtocolError, std::move(log.error()));
            const LogLevel level = clamp_level(log->level);
            if (Logger::enabled(level)) Logger::write(level, options.log_category, spec.session, std::move(log->text));
            return;
        }
        if (is_frame<common::CommandResult>(frame)) {
            Result<common::CommandResult> result = decode_contract<common::CommandResult>(frame.payload);
            if (!result) return end_child(ChildExitCause::ProtocolError, std::move(result.error()));
            return on_command_result(frame, *result);
        }
        if (is_frame<common::Unsupported>(frame)) {
            Result<common::Unsupported> unsupported = decode_contract<common::Unsupported>(frame.payload);
            if (!unsupported) return end_child(ChildExitCause::ProtocolError, std::move(unsupported.error()));
            return complete(unsupported->req_id, [&](const PendingRequest& request) -> Result<ReplyFrame> {
                return make_diag(ErrorDomain::Process, msg::kChildRequestUnsupported)
                    .arg("program", program)
                    .arg("frame_type", request.request_type)
                    .kind(ErrorKind::Unsupported)
                    .fail();
            });
        }
        const bool is_reply = std::ranges::any_of(pending, [&](const auto& entry) {
            return entry.second.reply_type == frame.type;
        });
        if (is_reply) return on_typed_reply(frame);
        if (requests != nullptr && requests->handles(frame.type)) {
            const std::optional<u64> req_id = read_req_id(frame.payload);
            if (!req_id)
                return end_child(ChildExitCause::ProtocolError,
                                 make_diag(ErrorDomain::Process, msg::kChildMalformedOutput).arg("program", program).build());
            requests->on_request(frame, ChildReply(owner, *req_id, generation));
            return;
        }
        observer.on_event(frame);
    }

    void on_command_result(const RawFrame& frame, const common::CommandResult& result) {
        complete(result.req_id, [&](const PendingRequest& request) -> Result<ReplyFrame> {
            if (!result.ok) {
                if (result.error) return std::unexpected(common::to_diagnostic(*result.error));
                return make_diag(ErrorDomain::Process, msg::kChildRequestFailed)
                    .arg("program", program)
                    .arg("frame_type", request.request_type)
                    .fail();
            }
            if (request.reply_type) return unexpected_reply(request, frame.type);
            return ReplyFrame{frame.type, std::vector<u8>(frame.payload.begin(), frame.payload.end())};
        });
    }

    void on_typed_reply(const RawFrame& frame) {
        const std::optional<u64> req_id = read_req_id(frame.payload);
        if (!req_id)
            return end_child(ChildExitCause::ProtocolError,
                             make_diag(ErrorDomain::Process, msg::kChildMalformedOutput).arg("program", program).build());
        complete(*req_id, [&](const PendingRequest& request) -> Result<ReplyFrame> {
            if (request.reply_type != frame.type) return unexpected_reply(request, frame.type);
            return ReplyFrame{frame.type, std::vector<u8>(frame.payload.begin(), frame.payload.end())};
        });
    }

    [[nodiscard]] Result<ReplyFrame> unexpected_reply(const PendingRequest& request, u64 reply_type) const {
        return make_diag(ErrorDomain::Process, msg::kChildUnexpectedReply)
            .arg("program", program)
            .arg("frame_type", request.request_type)
            .arg("reply_type", reply_type)
            .fail();
    }

    // Unknown req_ids are ignored: their requests already ended.
    template <class Answer>
    void complete(u64 req_id, Answer&& answer) {
        const auto found = pending.find(req_id);
        if (found == pending.end()) return;
        PendingRequest request = std::move(found->second);
        pending.erase(found);
        request.done(answer(request));
    }

    void send_request(u64 req_id, u64 request_type, std::vector<u8> frame, std::optional<u64> reply_type,
                      UniqueFunction<void(Result<ReplyFrame>)> done) {
        if (state != ChildState::Running || !open()) {
            Diagnostic error = make_diag(ErrorDomain::Process, msg::kChildNotRunning).arg("program", program).build();
            strand.post([alive = supervisor_alive.token(), done = std::move(done), error = std::move(error)]() mutable {
                if (!alive.cancelled()) done(std::unexpected(std::move(error)));
            });
            return;
        }
        pending.emplace(req_id, PendingRequest{request_type, reply_type, std::move(done)});
        channel->send(frame);
    }

    void end_child(ChildExitCause cause, Diagnostic error) {
        ending = cause;
        ending_error = std::move(error);
        hello_timer.cancel();
        ping_timer.cancel();
        kill_child();
    }

    void kill_child() {
        if (!child) return;
        Result<void> killed = child->terminate_tree();
        if (!killed && Logger::enabled(LogLevel::Warn))
            Logger::write(LogLevel::Warn, options.log_category, spec.session,
                          std::format("terminate_tree of {} (pid {}) failed: {}", program, child->pid(), killed.error().id));
    }

    void on_child_exit(ports::ChildExit status) {
        hello_timer.cancel();
        ping_timer.cancel();
        stop_timer.cancel();
        if (stderr_lines) stderr_lines->finish();
        generation_alive.cancel(CancelReason::Shutdown);

        ChildExitInfo info;
        info.generation = generation;
        info.pid = child ? std::optional<u32>(child->pid()) : std::nullopt;
        info.status = status;
        info.cause = ending.value_or(ChildExitCause::Exited);
        if (info.cause != ChildExitCause::Requested) {
            if (ending_error) info.error = std::move(*ending_error);
            else if (status.signal)
                info.error = make_diag(ErrorDomain::Process, msg::kChildSignaled)
                                 .arg("program", program)
                                 .arg("signal", *status.signal)
                                 .build();
            else
                info.error = make_diag(ErrorDomain::Process, msg::kChildExited)
                                 .arg("program", program)
                                 .arg("code", status.code.value_or(-1))
                                 .build();
        }

        if (spawned && record) record(*spawned, RecordChange::Exited);
        spawned.reset();
        channel.reset();
        stderr_lines.reset();
        child.reset();
        ending.reset();
        ending_error.reset();
        FlatMap<u64, PendingRequest> gone = std::move(pending);
        pending.clear();

        decide_after(info);
        // After decide_after, so a callback that calls stop() is not undone by it.
        fail_pending(gone);
        if (info.after == AfterExit::Restarting && state == ChildState::Stopped) {
            info.after = AfterExit::Stopped;
            info.restart_delay = {};
        }
        observer.on_exit(info);
    }

    void fail_pending(FlatMap<u64, PendingRequest>& gone) {
        for (auto& [req_id, request] : gone)
            request.done(make_diag(ErrorDomain::Process, msg::kChildGone)
                             .arg("program", program)
                             .arg("frame_type", request.request_type)
                             .fail());
    }

    // Sets info.after and the state; arms the restart timer when restarting.
    void decide_after(ChildExitInfo& info) {
        if (info.cause == ChildExitCause::Requested) {
            info.after = AfterExit::Stopped;
            state = ChildState::Stopped;
            return;
        }
        const bool mismatch = info.error && info.error->is(msg::kChildProtocolMismatch);
        if (!options.restart || mismatch) {
            info.after = AfterExit::Failed;
            state = ChildState::Failed;
            return;
        }
        const RestartPolicy& policy = *options.restart;
        const SteadyTime now = clock.steady_now();
        std::erase_if(restarts, [&](SteadyTime at) { return now - at >= policy.window; });
        const auto in_window = static_cast<u32>(restarts.size());
        if (!policy.allows(in_window)) {
            Diagnostic limit = make_diag(ErrorDomain::Process, msg::kChildRestartLimit)
                                   .arg("program", program)
                                   .arg("restarts", in_window)
                                   .arg("window", policy.window)
                                   .build();
            if (info.error) limit.causes.push_back(std::move(*info.error));
            info.error = std::move(limit);
            info.after = AfterExit::Failed;
            state = ChildState::Failed;
            return;
        }
        info.after = AfterExit::Restarting;
        info.restart_delay = policy.delay(in_window);
        restarts.push_back(now);
        state = ChildState::Restarting;
        restart_timer = timers.after(info.restart_delay, [this] { respawn(); });
    }

    void respawn() {
        if (state != ChildState::Restarting) return;
        Result<void> started = spawn();
        if (started) return;
        ChildExitInfo info;
        info.cause = ChildExitCause::SpawnFailed;
        info.generation = generation;
        info.error = std::move(started.error());
        decide_after(info);
        observer.on_exit(info);
    }

    bool stop(std::chrono::milliseconds grace) {
        if (!child) {
            restart_timer.cancel();
            state = ChildState::Stopped;
            return false;
        }
        if (ending == ChildExitCause::Requested) return true;
        const bool already_ending = ending.has_value();
        ending = ChildExitCause::Requested;
        ending_error.reset();
        hello_timer.cancel();
        ping_timer.cancel();
        state = ChildState::Stopping;
        // A child already being killed needs no grace.
        if (already_ending) return true;
        child->close_stdin();
        stop_timer = timers.after(grace, [this] { kill_child(); });
        return true;
    }
};

ChildSupervisor::ChildSupervisor(ports::IProcessLauncher& launcher, Executor& strand, TimerService& timers,
                                 const IClock& clock, ProcessSpec spec, ChildHandshake handshake,
                                 ChildSupervisorOptions options, ChildRequestHandler& requests, ChildObserver& observer,
                                 ChildRecordCallback record)
    : impl_(new Impl{.owner = *this,
                     .launcher = launcher,
                     .strand = strand,
                     .timers = timers,
                     .clock = clock,
                     .spec = std::move(spec),
                     .handshake = std::move(handshake),
                     .options = std::move(options),
                     .requests = &requests,
                     .observer = observer,
                     .record = std::move(record)}) {}

ChildSupervisor::ChildSupervisor(ports::IProcessLauncher& launcher, Executor& strand, TimerService& timers,
                                 const IClock& clock, ProcessSpec spec, ChildHandshake handshake,
                                 ChildSupervisorOptions options, ChildObserver& observer, ChildRecordCallback record)
    : impl_(new Impl{.owner = *this,
                     .launcher = launcher,
                     .strand = strand,
                     .timers = timers,
                     .clock = clock,
                     .spec = std::move(spec),
                     .handshake = std::move(handshake),
                     .options = std::move(options),
                     .requests = nullptr,
                     .observer = observer,
                     .record = std::move(record)}) {}

ChildSupervisor::~ChildSupervisor() {
    Impl& impl = *impl_;
    impl.generation_alive.cancel(CancelReason::Shutdown);
    impl.supervisor_alive.cancel(CancelReason::Shutdown);
    for (ChildReply* reply : impl.replies) reply->supervisor_ = nullptr;
    impl.replies.clear();
    if (!impl.child) return;
    // A child whose kill failed stays recorded, so the next start reaps it.
    if (impl.child->terminate_tree() && impl.spawned && impl.record) impl.record(*impl.spawned, RecordChange::Exited);
}

Result<void> ChildSupervisor::start() {
    Impl& impl = *impl_;
    if (impl.state != ChildState::Idle && impl.state != ChildState::Stopped && impl.state != ChildState::Failed)
        return make_diag(ErrorDomain::Process, msg::kChildAlreadyStarted)
            .arg("program", impl.program)
            .kind(ErrorKind::Conflict)
            .fail();
    impl.restarts.clear();
    Result<void> started = impl.spawn();
    if (!started) impl.state = ChildState::Failed;
    return started;
}

bool ChildSupervisor::stop(std::chrono::milliseconds grace) { return impl_->stop(grace); }

ChildState ChildSupervisor::state() const noexcept { return impl_->state; }

std::optional<u32> ChildSupervisor::pid() const noexcept {
    if (!impl_->spawned) return std::nullopt;
    return impl_->spawned->pid;
}

u32 ChildSupervisor::generation() const noexcept { return impl_->generation; }

u64 ChildSupervisor::next_req_id() noexcept { return ++impl_->last_req_id; }

void ChildSupervisor::send_request(u64 req_id, u64 request_type, std::vector<u8> frame, std::optional<u64> reply_type,
                                   UniqueFunction<void(Result<ReplyFrame>)> done) {
    impl_->send_request(req_id, request_type, std::move(frame), reply_type, std::move(done));
}

void ChildSupervisor::send_reply(u32 generation, std::vector<u8> frame) {
    if (generation == impl_->generation && impl_->open()) impl_->channel->send(frame);
}

void ChildSupervisor::track(ChildReply& reply) { impl_->replies.push_back(&reply); }

void ChildSupervisor::untrack(ChildReply& reply) noexcept { std::erase(impl_->replies, &reply); }

void ChildSupervisor::retrack(ChildReply& from, ChildReply& to) noexcept {
    std::ranges::replace(impl_->replies, &from, &to);
}

}  // namespace reboot::process
