#pragma once

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "reboot/builds/build_usage.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/sessions/session_exit.hpp"
#include "reboot/sessions/session_info.hpp"
#include "reboot/sessions/session_spec.hpp"
#include "reboot/sessions/stop_request.hpp"

namespace reboot {
class IClock;
class IRandom;
class Executor;
class TimerService;
class EventBus;
}  // namespace reboot

namespace reboot::sessions {

class ISessionDriver;

// Capabilities: game-launch.instance-registry, game-launch.+5.
// Strand-only. Every play and host session the engine owns; every end, a self-exit included, goes through stop.
class SessionRegistry final : public builds::IBuildUsage {
public:
    // `strand` runs completions and destroys drivers outside their own calls.
    SessionRegistry(IClock& clock, IRandom& random, Executor& strand, TimerService& timers, EventBus& events);
    ~SessionRegistry() override;
    SessionRegistry(const SessionRegistry&) = delete;
    SessionRegistry& operator=(const SessionRegistry&) = delete;

    // Starts Preparing. sessions.parent_not_live unless the parent is a play session in Preparing..Running.
    Result<SessionId> open(SessionSpec spec, std::unique_ptr<ISessionDriver> driver);

    // Between Preparing, Launching, Loading and Running only; sessions.invalid_transition otherwise.
    Result<void> set_phase(SessionId session, SessionPhase phase);
    Result<void> set_lease(SessionId session, Lease lease);

    // A primary spawn starts the next incarnation, unless begin_respawn already did.
    Result<Incarnation> note_spawned(SessionId session, SpawnedProcess process);
    // Call before killing the primary to respawn it, so its exit counts as an earlier incarnation's.
    // Returns the respawned primary's incarnation, also for reporting a failed respawn.
    Result<Incarnation> begin_respawn(SessionId session);
    void note_process_exited(SessionId session, SpawnedProcess process);

    // Raising a condition with the same message id replaces it.
    Result<void> raise_degraded(SessionId session, Diagnostic condition);
    Result<void> clear_degraded(SessionId session, std::string_view message_id);

    // The current primary ended on its own; an earlier incarnation's exit is ignored.
    void report_exit(SessionId session, Incarnation incarnation, SessionExit exit);

    // Idempotent and the first reason wins; `on_ended` is posted after SessionEnded, also for an ended id.
    Result<void> stop(SessionId session, StopRequest request, UniqueFunction<void()> on_ended);
    // Every live session, or every one of `kind`; `on_ended` is posted once all of them ended.
    void stop_all(std::optional<SessionKind> kind, StopRequest request, UniqueFunction<void()> on_ended);

    // Stops this connection's Client-leased sessions with LeaseEnded.
    void on_connection_closed(ConnectionId connection);
    // Shutdown step RefuseNew: later open() calls fail with sessions.refusing_new.
    void refuse_new() noexcept;

    // Feeds the update gate and the idle exit.
    [[nodiscard]] bool has_live() const noexcept;

    [[nodiscard]] std::vector<SessionInfo> list() const;
    [[nodiscard]] Result<SessionInfo> get(SessionId session) const;

    [[nodiscard]] std::vector<SessionId> sessions_using(BuildId build) const override;
    // Stops them with BuildRemoved and the default grace.
    void stop_sessions_using(BuildId build, UniqueFunction<void(Result<void>)> done) override;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace reboot::sessions
