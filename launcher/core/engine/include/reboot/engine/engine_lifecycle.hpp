#pragma once

#include <optional>

#include "reboot/contracts/ipc.hpp"
#include "reboot/engine/engine_origin.hpp"
#include "reboot/engine/idle_policy.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/sessions/shutdown_cause.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/storage/resume_document.hpp"
#include "reboot/updates/activity_probe.hpp"

namespace reboot {
class EventBus;
class TimerService;
}  // namespace reboot

namespace reboot::sessions {
class SessionRegistry;
class ShutdownCoordinator;
}  // namespace reboot::sessions

namespace reboot::engine {

class EngineActivityProbe;

enum class EnginePhase : u8 { Running, Draining, ShuttingDown, Updating };
enum class DrainReason : u8 { Update, UserStop, Replace };
enum class ShutdownWhen : u8 { Now, WhenIdle };

// Restart is EX_TEMPFAIL: systemd and launchd start the engine again, elsewhere the next client does.
enum class EngineExit : u8 { Ok = 0, Restart = 75 };

// EventKind::EngineState, coalesced; published on every phase change and live-work change.
struct EngineStateEvent {
    EnginePhase phase = EnginePhase::Running;
    u32 sessions = 0;
    u32 operations = 0;

    bool operator==(const EngineStateEvent&) const = default;
};

struct EngineLifecycleDeps {
    EngineOrigin origin{};
    EngineActivityProbe& activity;
    sessions::SessionRegistry& sessions;
    sessions::ShutdownCoordinator& shutdown;
    // restart_when_idle records the origin here, so a client-started successor stays resident.
    storage::DocumentStore<storage::ResumeDocument>& resume;
    EventBus& events;
    TimerService& timers;
    // UpdateService::drain_consented; UpdateService is built after the lifecycle.
    UniqueFunction<Result<void>()> drain_consented;
    // Runs on the strand once the shutdown steps finished; EngineHost then stops the strand.
    UniqueFunction<void(EngineExit)> on_exit;
};

// Capabilities: none. Strand-only; the engine's phase and every way it ends, never a closing UI.
// It is UpdateService's IActivityProbe, so it handles each activity change before passing it on.
class EngineLifecycle final : public updates::IActivityProbe {
public:
    explicit EngineLifecycle(EngineLifecycleDeps deps);
    ~EngineLifecycle() override;
    EngineLifecycle(const EngineLifecycle&) = delete;
    EngineLifecycle& operator=(const EngineLifecycle&) = delete;

    // Step 9: Running, EngineState published, idle evaluation armed.
    void start();

    [[nodiscard]] EnginePhase phase() const noexcept { return phase_; }
    [[nodiscard]] EngineOrigin origin() const noexcept { return deps_.origin; }

    // engine.shutting_down once draining or shutting down.
    [[nodiscard]] Result<void> admit_new_work() const;

    // The same reason again succeeds, another is engine.already_draining. Replace needs an idle
    // OnDemand engine (engine.not_replaceable, engine.busy), so it never stops anyone's work.
    Result<void> drain(DrainReason reason);
    // Now always runs. A when-idle exit of the other kind already waiting is engine.exit_pending.
    Result<void> shutdown(ShutdownWhen when);
    Result<void> restart_when_idle();
    // SIGINT or SIGTERM.
    void on_os_signal();

    void on_connected(ConnectionId connection, contracts::ipc::ClientKind kind);
    void on_disconnected(ConnectionId connection);

    // UpdateServiceDeps::drain_for_update.
    void drain_for_update();
    // UpdateServiceDeps::quiesce.
    void quiesce_for_update(UniqueFunction<Result<void>()> apply);

    [[nodiscard]] updates::ActivitySnapshot snapshot() const override;
    void set_on_change(UniqueFunction<void()> on_change) override;

private:
    void set_phase(EnginePhase phase);
    void on_activity_changed();
    Result<void> exit_when_idle(sessions::ShutdownCause cause, EngineExit exit);
    void run_shutdown(sessions::ShutdownCause cause, EngineExit exit);

    EngineLifecycleDeps deps_;
    IdlePolicy idle_;
    EnginePhase phase_ = EnginePhase::Running;
    std::optional<DrainReason> draining_;
    std::optional<EngineExit> pending_exit_;
    UniqueFunction<void()> update_listener_;
};

}  // namespace reboot::engine
