#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "reboot/backend/account_prune_filter.hpp"
#include "reboot/backend/account_registration.hpp"
#include "reboot/backend/backend_account.hpp"
#include "reboot/backend/backend_config.hpp"
#include "reboot/backend/backend_health.hpp"
#include "reboot/backend/backend_session_config.hpp"
#include "reboot/backend/launch_credential.hpp"
#include "reboot/contracts/backend.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/process/child_record.hpp"
#include "reboot/process/child_supervisor.hpp"
#include "reboot/process/process_spec.hpp"

namespace reboot {
class Executor;
class IClock;
class Redactor;
class TimerService;
}  // namespace reboot

namespace reboot::ports {
class IProcessLauncher;
}

namespace reboot::backend {

class IBackendProcessObserver;
class IMatchTargetResolver;

// Capabilities: auth-backend.embedded-process, auth-backend.console-key, matchmaking-networking.matchmaker-address-sync.
// Strand-only. contracts/backend.hpp over one ChildSupervisor:
// - the real child runs in its own Job or process group; stderr lines go to the engine log under
//   LogCategory::Backend and are never fatal; there is no console window;
// - crashes and hangs restart under RestartPolicy (1 s to 30 s, at most 5 in 5 minutes, then Failed);
// - after each Ready it replays pending renames, then RegisterAccount, then every live
//   ConfigureSession, and only then reports on_ready; replies from an older generation are dropped;
// - ResolveMatchTarget is answered from the installed IMatchTargetResolver, or with no endpoint.
// What the child itself must do is in contracts/backend.hpp.
class BackendProcess {
public:
    // `log_level` goes in every Welcome.
    BackendProcess(ports::IProcessLauncher& launcher, Executor& strand, TimerService& timers, const IClock& clock,
                   Redactor& redactor, process::ProcessSpec spec, process::ChildRecordCallback record,
                   LogLevel log_level);
    // Kills a live child at once.
    ~BackendProcess();
    BackendProcess(const BackendProcess&) = delete;
    BackendProcess& operator=(const BackendProcess&) = delete;

    void set_observer(IBackendProcessObserver* observer);
    void set_match_target_resolver(IMatchTargetResolver* resolver);
    // AccountRenameConflict events, raised when a rename with RenameConflictPolicy::Report fails.
    void set_rename_conflict_handler(UniqueFunction<void(const contracts::backend::AccountRenameConflict&)> handler);

    // From Stopped or Failed. Welcome carries config.bind_address(). A spawn error is returned
    // here and no on_exit follows.
    Result<void> start(const BackendConfig& config);
    // Closes stdin, waits `grace`, then kills the tree. False when nothing is alive.
    bool stop(std::chrono::milliseconds grace = default_deadline(OpKind::GracefulStop));

    [[nodiscard]] process::ChildState state() const noexcept;
    [[nodiscard]] u32 generation() const noexcept;
    [[nodiscard]] std::optional<u32> pid() const noexcept;

    // Replay set. Registering a record again replaces its account id. Sent at once when Running.
    void register_account(AccountRegistration account);
    // Kept and replayed until end_session. Not Running: stored, and `done` succeeds at once.
    void configure_session(SessionId session, BackendSessionConfig config, UniqueFunction<void(Result<void>)> done);
    // Revokes the session's unredeemed credentials and the tokens they were redeemed for.
    void end_session(SessionId session, UniqueFunction<void(Result<void>)> done);
    // Deferred while a live session is configured with `old_account_id` or the backend is not
    // Running; sent when the last such session ends or at the next generation.
    void rename_account(std::string old_account_id, std::string new_account_id,
                        contracts::backend::RenameConflictPolicy on_conflict, UniqueFunction<void(Result<void>)> done);

    // Running only, else process.child_not_running; `done` runs on the strand exactly once.
    void health(UniqueFunction<void(Result<BackendHealth>)> done);
    // Bound to `session`, which must be configured (else internal.bug).
    void mint_launch_credential(SessionId session, LaunchCredentialRequest request,
                                UniqueFunction<void(Result<LaunchCredential>)> done);
    void list_accounts(UniqueFunction<void(Result<std::vector<BackendAccount>>)> done);
    void reset_account(std::string account_id, UniqueFunction<void(Result<void>)> done);
    void delete_account(std::string account_id, UniqueFunction<void(Result<void>)> done);
    // Answers with the accounts it removed.
    void prune_accounts(AccountPruneFilter filter, UniqueFunction<void(Result<std::vector<BackendAccount>>)> done);
    void purge_data(UniqueFunction<void(Result<void>)> done);
    void drain(UniqueFunction<void(Result<void>)> done);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::backend
