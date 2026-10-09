#pragma once

#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/process/child_handshake.hpp"
#include "reboot/process/child_record.hpp"
#include "reboot/process/child_reply.hpp"
#include "reboot/process/liveness_policy.hpp"
#include "reboot/process/process_spec.hpp"
#include "reboot/process/restart_policy.hpp"

namespace rb::ports {
class IProcessLauncher;
}

namespace rb::process {

class ChildObserver;
class ChildRequestHandler;

enum class ChildState : u8 { Idle, Starting, Running, Restarting, Stopping, Stopped, Failed };

struct ChildSupervisorOptions {
    // Child Log frames and stderr lines are logged here, tagged with the spec's session.
    LogCategory log_category = LogCategory::Engine;
    // stderr is only ever logged, never fatal.
    LogLevel stderr_level = LogLevel::Warn;
    std::chrono::milliseconds hello_timeout = default_deadline(OpKind::ChildHello);
    LivenessPolicy liveness;
    // Present: crashes and hangs restart under it (backend). Absent: they are only reported (game server).
    std::optional<RestartPolicy> restart;
};

// Capabilities: game-launch.process-spawn-helper.
// Strand-only. Owns one native child at a time, spawned from `spec` through IProcessLauncher (its
// own Job or process group) with a ChildChannel on stdio; stderr goes through a LineReader to the
// log. It pings per LivenessPolicy, correlates req_id in both directions so every request ends in
// exactly one reply, CommandResult or Unsupported, and reports each spawn and exit to `record`.
class ChildSupervisor {
public:
    ChildSupervisor(ports::IProcessLauncher& launcher, Executor& strand, TimerService& timers, const IClock& clock,
                    ProcessSpec spec, ChildHandshake handshake, ChildSupervisorOptions options,
                    ChildRequestHandler& requests, ChildObserver& observer, ChildRecordCallback record);
    // For a child that sends no requests: every frame that answers none of ours goes to on_event.
    ChildSupervisor(ports::IProcessLauncher& launcher, Executor& strand, TimerService& timers, const IClock& clock,
                    ProcessSpec spec, ChildHandshake handshake, ChildSupervisorOptions options,
                    ChildObserver& observer, ChildRecordCallback record);
    // Kills a live child at once, without on_exit, detaches every outstanding ChildReply and drops
    // the callbacks of pending requests uncalled.
    ~ChildSupervisor();
    ChildSupervisor(const ChildSupervisor&) = delete;
    ChildSupervisor& operator=(const ChildSupervisor&) = delete;

    // From Idle, Stopped or Failed; resets the restart window. A spawn error is returned here and
    // no on_exit follows; a respawn that fails ends in on_exit with SpawnFailed.
    Result<void> start();

    // Closes stdin, waits `grace`, then terminate_tree; on_exit reports Requested. Returns false
    // when no child is alive (Idle, Restarting, Stopped, Failed): the state is then Stopped at once
    // and no on_exit follows.
    bool stop(std::chrono::milliseconds grace = default_deadline(OpKind::GracefulStop));

    [[nodiscard]] ChildState state() const noexcept;
    [[nodiscard]] std::optional<u32> pid() const noexcept;
    [[nodiscard]] u32 generation() const noexcept;

    // Running only, else process.child_not_running. `done` runs later on the strand, exactly once,
    // with the Reply, the child's CommandResult error, process.child_request_unsupported, or
    // process.child_gone when the child exits first.
    template <ContractMessage Reply, CorrelatedMessage Request>
    void request(Request message, UniqueFunction<void(Result<Reply>)> done);

    // For requests the child answers with a CommandResult only.
    template <CorrelatedMessage Request>
    void command(Request message, UniqueFunction<void(Result<void>)> done);

private:
    friend class ChildReply;

    // The frame that answered a request: the typed reply, or an ok CommandResult for command().
    struct ReplyFrame {
        u64 type = 0;
        std::vector<u8> payload;
    };

    [[nodiscard]] u64 next_req_id() noexcept;
    // No `reply_type` means only an ok CommandResult completes the request.
    void send_request(u64 req_id, u64 request_type, std::vector<u8> frame, std::optional<u64> reply_type,
                      UniqueFunction<void(Result<ReplyFrame>)> done);
    // Dropped when `generation` is no longer the live child.
    void send_reply(u32 generation, std::vector<u8> frame);
    // Outstanding replies, so the destructor can detach them.
    void track(ChildReply& reply);
    void untrack(ChildReply& reply) noexcept;
    void retrack(ChildReply& from, ChildReply& to) noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

template <ContractMessage Reply, CorrelatedMessage Request>
void ChildSupervisor::request(Request message, UniqueFunction<void(Result<Reply>)> done) {
    message.req_id = next_req_id();
    const u64 req_id = message.req_id;
    send_request(req_id, contract_frame_type_v<Request>, encode_contract_frame(message), contract_frame_type_v<Reply>,
                 [done = std::move(done)](Result<ReplyFrame> reply) mutable {
                     if (!reply) return done(std::unexpected(std::move(reply.error())));
                     done(decode_contract<Reply>(reply->payload));
                 });
}

template <CorrelatedMessage Request>
void ChildSupervisor::command(Request message, UniqueFunction<void(Result<void>)> done) {
    message.req_id = next_req_id();
    const u64 req_id = message.req_id;
    send_request(req_id, contract_frame_type_v<Request>, encode_contract_frame(message), std::nullopt,
                 [done = std::move(done)](Result<ReplyFrame> reply) mutable {
                     if (!reply) return done(std::unexpected(std::move(reply.error())));
                     done(Result<void>{});
                 });
}

}  // namespace rb::process
