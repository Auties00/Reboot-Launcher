#pragma once

#include <memory>

#include "reboot/backend/backend_config.hpp"
#include "reboot/backend/backend_lease.hpp"
#include "reboot/backend/backend_session_config.hpp"
#include "reboot/backend/backend_state.hpp"
#include "reboot/backend/backend_stop_cause.hpp"
#include "reboot/backend/backend_upstream.hpp"
#include "reboot/backend/launch_credential.hpp"
#include "reboot/backend/login_observed_event.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot {
class EventBus;
class Executor;
class TimerService;
class UserRequestRegistry;
}  // namespace reboot

namespace reboot::net {
class HostTlsMemory;
}

namespace reboot::backend {

class BackendProcess;
class IBackendSessions;
class RemoteBackendProbe;

// What a reconfigure does to live sessions; api::RunningPolicy maps onto it.
enum class RunningPolicy : u8 { Refuse, StopSessions };

// AfterLeases: the config waits in BackendState::pending_config for the last lease.
enum class ReconfigureTiming : u8 { Now, AfterLeases };

// Capabilities: auth-backend.type-config, auth-backend.start-state-machine, auth-backend.stop-bookkeeping, auth-backend.ping, auth-backend.+13, auth-backend.+14, auth-backend.+16, auth-backend.+56, auth-backend.+68, auth-backend.+87.
// Strand-only; the one owner of the backend's state. The CLI's backend start, stop and status
// (+16, +56) are start_backend, start_stop and state() through the API like any UI's.
// - Leases are counted: the first starts the backend, the last stops it unless the user pinned it.
// - One transition runs at a time. A start, stop or reconfigure arriving mid-transition is queued
//   and applied in order; a start during a start joins it. Every op completes, a superseded one
//   with Cancelled{Superseded}.
// - Every path ends in Running, Stopped or Failed and publishes BackendEvent.
// - Embedded spawns through BackendProcess. Local and Remote spawn nothing and are probed, and
//   nothing here stops a process it did not spawn.
// - A LegacyFixed session leases like any other: the one-session slot, the exclusive bind of
//   3551 and :80 and PortBusy without killing the owner are front::LegacyFixedListeners'.
// - Readiness is a health check made when asked, never a remembered flag.
class BackendService {
public:
    BackendService(BackendConfig config, BackendProcess& process, RemoteBackendProbe& probe, IBackendSessions& sessions,
                   OpRegistry& ops, UserRequestRegistry& requests, net::HostTlsMemory& tls, EventBus& events,
                   Executor& strand, TimerService& timers);
    ~BackendService();
    BackendService(const BackendService&) = delete;
    BackendService& operator=(const BackendService&) = delete;

    [[nodiscard]] const BackendState& state() const noexcept;

    // Backend.SetTarget and settings changes. With no lease it applies now: a running backend is
    // stopped and started again on the new config if pinned. With a session lease, Refuse fails
    // with backend.in_use and StopSessions stops those sessions through IBackendSessions; the
    // config then waits for the last lease, maintenance ones included, and replaces any pending
    // one. A failed stop drops it and sets last_error. Fails with backend.shutting_down.
    Result<ReconfigureTiming> reconfigure(BackendConfig config, RunningPolicy running);

    // Backend.Start; completes with the BackendUpstream once healthy. A plain-http remote raises
    // ConfirmUnencryptedUpstream until HostTlsMemory remembers the answer.
    Result<OpHandle> start_backend(bool pin, DisconnectPolicy policy);
    // Backend.Stop: clears the pin and stops. Fails with backend.in_use while a session lease is
    // live; the engine stops those sessions first.
    Result<OpHandle> start_stop(DisconnectPolicy policy);

    // Starts the backend when needed, from Failed too with a fresh restart window. Fails with
    // backend.shutting_down, or backend.reconfiguring (retryable) while a config is pending, so
    // new sessions cannot hold off a reconfigure forever.
    Result<BackendLease> acquire(SessionId session);
    // Account administration and purge; fails with backend.embedded_only, or
    // backend.reconfiguring while a config is pending.
    Result<BackendLease> acquire_maintenance();

    // Waits for Ready under the BackendReady deadline when starting, then health-checks: Health
    // for Embedded, RemoteBackendProbe otherwise. `done` runs on the strand exactly once.
    void ensure_ready(const BackendLease& lease, CancelToken token, UniqueFunction<void(Result<BackendUpstream>)> done);

    // Embedded: ConfigureSession for the lease's session, replayed after every restart until the
    // lease is released. Local and Remote: succeeds with nothing to send.
    void configure_session(const BackendLease& lease, BackendSessionConfig config,
                           UniqueFunction<void(Result<void>)> done);
    // Embedded only (backend.embedded_only), after configure_session; bound to the lease's session.
    void mint_launch_credential(const BackendLease& lease, LaunchCredentialRequest request,
                                UniqueFunction<void(Result<LaunchCredential>)> done);

    // For storage::ResetHooks (cause Reset) and the ShutdownCoordinator (cause Shutdown): clears
    // the pin and stops now. Reset fails with backend.in_use while a session lease is live.
    void stop_for(BackendStopCause cause, UniqueFunction<void(Result<void>)> done);

    // Fallback LoggedIn signal for sessions without our client DLL.
    void set_login_observer(UniqueFunction<void(const LoginObservedEvent&)> observer);
    // Runs after every transition to Running; BackendAccounts refreshes its list there.
    void add_ready_listener(UniqueFunction<void(const BackendState&)> listener);

private:
    friend class BackendLease;
    void release(u64 lease_id);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::backend
