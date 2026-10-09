#include "reboot/engine/engine_lifecycle.hpp"

#include <limits>
#include <string_view>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/engine/engine_activity_probe.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/sessions/session_info.hpp"
#include "reboot/sessions/session_kind.hpp"
#include "reboot/sessions/session_phase.hpp"
#include "reboot/sessions/session_registry.hpp"
#include "reboot/sessions/shutdown_coordinator.hpp"
#include "reboot/sessions/shutdown_report.hpp"
#include "reboot/sessions/stop_reason.hpp"
#include "reboot/sessions/stop_request.hpp"

namespace rb::engine {

namespace {

[[nodiscard]] constexpr std::string_view reason_name(DrainReason reason) noexcept {
    switch (reason) {
        case DrainReason::Update: return "update";
        case DrainReason::UserStop: return "user_stop";
        case DrainReason::Replace: return "replace";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view exit_name(EngineExit exit) noexcept {
    return exit == EngineExit::Restart ? "restart" : "exit";
}

constexpr std::size_t kUpdateFailureBudget = std::size_t{64} << 10;

[[nodiscard]] Diagnostic shutting_down() {
    return make_diag(ErrorDomain::Engine, msg::kShuttingDown).kind(ErrorKind::Conflict);
}

}  // namespace

EngineLifecycle::EngineLifecycle(EngineLifecycleDeps deps)
    : deps_(std::move(deps)),
      idle_(deps_.timers, deps_.origin, [this] {
          // An op started without an event may be live although nothing said so.
          if (!snapshot().idle() || deps_.activity.connections() != 0) return on_activity_changed();
          run_shutdown(sessions::ShutdownCause::Idle, EngineExit::Ok);
      }) {
    deps_.activity.set_on_change([this] { on_activity_changed(); });
    update_failures_ = deps_.events.subscribe(EventFilter{{EventKind::UpdateFailed}, std::nullopt, std::nullopt},
                                              kUpdateFailureBudget);
    update_failures_->set_notify([this] {
        std::vector<EventEnvelope> failures;
        update_failures_->drain(failures, std::numeric_limits<std::size_t>::max());
        update_failures_->take_resync();
        on_update_failed();
    });
}

EngineLifecycle::~EngineLifecycle() {
    update_failures_->set_notify({});
    deps_.activity.set_on_change({});
    idle_.disable();
}

void EngineLifecycle::start() {
    phase_ = EnginePhase::Running;
    on_activity_changed();
}

Result<void> EngineLifecycle::admit_new_work() const {
    if (phase_ != EnginePhase::Running) return std::unexpected(shutting_down());
    return {};
}

Result<void> EngineLifecycle::drain(DrainReason reason) {
    if (draining_) {
        if (*draining_ == reason) return {};
        return make_diag(ErrorDomain::Engine, msg::kAlreadyDraining)
            .arg("reason", reason_name(*draining_))
            .kind(ErrorKind::Conflict)
            .fail();
    }
    if (phase_ == EnginePhase::ShuttingDown || phase_ == EnginePhase::Updating) return std::unexpected(shutting_down());
    switch (reason) {
        case DrainReason::Update:
            // UpdateService answers through drain_for_update, which records the drain.
            if (Result<void> consented = deps_.drain_consented ? deps_.drain_consented() : Result<void>{}; !consented)
                return consented;
            draining_ = DrainReason::Update;
            return {};
        case DrainReason::UserStop:
            draining_ = DrainReason::UserStop;
            set_phase(EnginePhase::Draining);
            run_shutdown(sessions::ShutdownCause::DrainUserStop, EngineExit::Ok);
            return {};
        case DrainReason::Replace:
            if (!replaceable(deps_.origin))
                return make_diag(ErrorDomain::Engine, msg::kNotReplaceable)
                    .arg("origin", origin_name(deps_.origin))
                    .kind(ErrorKind::Conflict)
                    .fail();
            if (!snapshot().idle()) return make_diag(ErrorDomain::Engine, msg::kBusy).kind(ErrorKind::Conflict).fail();
            draining_ = DrainReason::Replace;
            set_phase(EnginePhase::Draining);
            run_shutdown(sessions::ShutdownCause::DrainReplace, EngineExit::Ok);
            return {};
    }
    return {};
}

Result<void> EngineLifecycle::shutdown(ShutdownWhen when) {
    if (when == ShutdownWhen::Now) {
        run_shutdown(sessions::ShutdownCause::Requested, EngineExit::Ok);
        return {};
    }
    return exit_when_idle(sessions::ShutdownCause::Requested, EngineExit::Ok);
}

Result<void> EngineLifecycle::restart_when_idle() {
    if (phase_ == EnginePhase::ShuttingDown || phase_ == EnginePhase::Updating) return std::unexpected(shutting_down());
    if (pending_exit_ && *pending_exit_ != EngineExit::Restart)
        return make_diag(ErrorDomain::Engine, msg::kExitPending)
            .arg("exit", exit_name(*pending_exit_))
            .kind(ErrorKind::Conflict)
            .fail();
    Result<u64> recorded = deps_.resume.update([origin = deps_.origin](storage::ResumeDocument& document) {
        document.origin = origin;
    });
    if (!recorded) return std::unexpected(std::move(recorded.error()));
    return exit_when_idle(sessions::ShutdownCause::Requested, EngineExit::Restart);
}

void EngineLifecycle::on_os_signal() { run_shutdown(sessions::ShutdownCause::OsSignal, EngineExit::Ok); }

void EngineLifecycle::on_connected(ConnectionId connection, contracts::ipc::ClientKind kind) {
    deps_.activity.on_connected(connection, kind);
    on_activity_changed();
}

void EngineLifecycle::on_disconnected(ConnectionId connection) {
    deps_.activity.on_disconnected(connection);
    on_activity_changed();
}

void EngineLifecycle::drain_for_update() {
    if (phase_ == EnginePhase::ShuttingDown || phase_ == EnginePhase::Updating) return;
    draining_ = DrainReason::Update;
    set_phase(EnginePhase::Draining);
    sessions::StopRequest stop;
    stop.reason = sessions::StopReason::Update;
    deps_.sessions.stop_all(sessions::SessionKind::Play, std::move(stop), [] {});
    if (deps_.drain_hosts) deps_.drain_hosts(sessions::ShutdownCause::DrainUpdate, [] {});
}

void EngineLifecycle::quiesce_for_update(UniqueFunction<Result<void>()> apply) {
    if (phase_ == EnginePhase::ShuttingDown || phase_ == EnginePhase::Updating) return;
    set_phase(EnginePhase::Updating);
    idle_.disable();
    pending_exit_ = EngineExit::Restart;
    deps_.shutdown.run(sessions::ShutdownCause::DrainUpdate,
                       [this, apply = std::move(apply)](const sessions::ShutdownReport&) mutable {
                           // Only a failed apply returns; the next start tries the update again.
                           if (Result<void> applied = apply(); !applied)
                               REBOOT_LOG_ERROR(Update, "the update could not be applied: {}", applied.error().id);
                           if (deps_.on_exit) deps_.on_exit(EngineExit::Restart);
                       });
}

updates::ActivitySnapshot EngineLifecycle::snapshot() const { return deps_.activity.snapshot(); }

void EngineLifecycle::set_on_change(UniqueFunction<void()> on_change) { update_listener_ = std::move(on_change); }

void EngineLifecycle::publish_state() {
    EngineStateEvent state;
    state.phase = phase_;
    for (const sessions::SessionInfo& session : deps_.sessions.list())
        if (sessions::is_live(session.phase)) ++state.sessions;
    state.operations = static_cast<u32>(deps_.ops.live().size());
    if (published_ == state) return;
    published_ = state;
    deps_.events.publish(EventKind::EngineState, state, EventScope{.coalesce_key = "engine"});
}

void EngineLifecycle::set_phase(EnginePhase phase) {
    if (phase_ == phase) return;
    phase_ = phase;
    publish_state();
}

void EngineLifecycle::on_activity_changed() {
    const updates::ActivitySnapshot now = snapshot();
    idle_.update(deps_.activity.connections(), !now.idle());
    publish_state();
    if (update_listener_) update_listener_();
}

void EngineLifecycle::on_update_failed() {
    if (draining_ != DrainReason::Update || phase_ == EnginePhase::ShuttingDown || phase_ == EnginePhase::Updating) return;
    draining_.reset();
    set_phase(EnginePhase::Running);
}

Result<void> EngineLifecycle::exit_when_idle(sessions::ShutdownCause cause, EngineExit exit) {
    if (phase_ == EnginePhase::ShuttingDown || phase_ == EnginePhase::Updating) return std::unexpected(shutting_down());
    if (pending_exit_) {
        if (*pending_exit_ == exit) return {};
        return make_diag(ErrorDomain::Engine, msg::kExitPending)
            .arg("exit", exit_name(*pending_exit_))
            .kind(ErrorKind::Conflict)
            .fail();
    }
    pending_exit_ = exit;
    idle_.when_work_idle([this, cause, exit] { run_shutdown(cause, exit); });
    return {};
}

void EngineLifecycle::run_shutdown(sessions::ShutdownCause cause, EngineExit exit) {
    if (phase_ == EnginePhase::ShuttingDown || phase_ == EnginePhase::Updating) return;
    set_phase(EnginePhase::ShuttingDown);
    idle_.disable();
    pending_exit_ = exit;
    deps_.shutdown.run(cause, [this, exit](const sessions::ShutdownReport&) {
        if (deps_.on_exit) deps_.on_exit(exit);
    });
}

}  // namespace rb::engine
