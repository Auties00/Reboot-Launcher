#pragma once

#include <chrono>
#include <memory>
#include <optional>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/storage/resume_document.hpp"
#include "reboot/storage/settings_values.hpp"
#include "reboot/storage/state_document.hpp"
#include "reboot/updates/pending_update_marker.hpp"
#include "reboot/updates/resume_record.hpp"
#include "reboot/updates/update_state.hpp"

namespace rb {
class EventBus;
class Executor;
class IClock;
class TimerService;
class UserRequestRegistry;
class WorkerPool;
}  // namespace rb

namespace rb::ports {
class IFileSystem;
class IUpdateApplier;
}  // namespace rb::ports

namespace rb::net {
class ResumableDownloader;
}

namespace rb::components {
class ManifestService;
}

namespace rb::updates {

class IActivityProbe;

struct UpdateServiceDeps {
    ports::IUpdateApplier& applier;
    ports::IFileSystem& fs;
    components::ManifestService& manifest;
    net::ResumableDownloader& downloader;
    storage::DocumentStore<storage::ResumeDocument>& resume;
    // last_check and the version last announced in NotifyOnly mode.
    storage::DocumentStore<storage::StateDocument>& state;
    IActivityProbe& activity;
    WorkerPool& workers;
    Executor& strand;
    const IClock& clock;
    TimerService& timers;
    OpRegistry& ops;
    EventBus& events;
    UserRequestRegistry& requests;
    // Drain{Update}: stop play and drain hosts per host.update_policy.
    UniqueFunction<void()> drain_for_update;
    // The shutdown steps for a restart, then `apply`; if that fails the engine exits with EngineExit::Restart.
    UniqueFunction<void(UniqueFunction<Result<void>()> apply)> quiesce;
};

struct UpdateOptions {
    SemVer installed;
    contracts::ipc::EngineOrigin origin{};
    storage::UpdateChannel channel = storage::UpdateChannel::Stable;
    bool auto_check = true;
    // The engine is resident, so the startup check alone would go stale.
    std::chrono::hours check_interval{6};
    // Linux: the shim counted this start in the marker before exec.
    bool shim_counts_attempts = false;
};

// The result of the startup step that reads the marker and resume.json.
struct StartupResume {
    std::optional<MarkerVerdict> verdict;
    std::optional<PendingUpdateMarker> marker;
    std::optional<ResumeRecord> resume;
};

// Capabilities: launcher-updates.check.
// Strand-only; downloads, hashing and marker I/O run on the WorkerPool. The engine is the only updater.
// - Only the app track is applied here; payloads and runtimes reach new sessions through ComponentStore.
// - min_supported refuses new sessions only; an update never stops a running one.
// - NotifyOnly announces each version once and never downloads.
// - In-place restart with --resume on every OS: update-mechanism overrides release-pipeline and process-model.
// - Headless hosts are not NotifyOnly: the server profile's host.update_policy=manual keeps the gate closed.
// - Gate open: phase Applying, resume.json and marker, EngineUpdating, quiesce, IUpdateApplier.
// - A failed apply keeps the marker and resume.json; the old version's next start reports NotApplied.
class UpdateService {
public:
    UpdateService(UpdateServiceDeps deps, UpdateOptions options, const AppLayout& layout);
    ~UpdateService();
    UpdateService(const UpdateService&) = delete;
    UpdateService& operator=(const UpdateService&) = delete;

    // Blocking, before the strand. Fails only on unrecoverable I/O; a malformed marker is removed like Foreign.
    [[nodiscard]] Result<StartupResume> begin_startup();
    // On updates.self_test_failed the engine exits with EngineExit::Restart; the next start is another attempt.
    [[nodiscard]] Result<void> confirm_startup(Result<void> self_test);

    // Runs the Startup check and arms the periodic one, which skips while an apply is armed.
    void start();
    void set_auto_check(bool enabled);
    // Re-selects the offer from the manifest in use; a staged or applying update is kept.
    void set_channel(storage::UpdateChannel channel);

    [[nodiscard]] UpdateState state() const;

    // Completes with std::optional<UpdateOffer>; an InPlace offer then starts start_apply(WhenIdle).
    // updates.busy while Applying.
    [[nodiscard]] Result<OpHandle> start_check(CheckTrigger trigger, DisconnectPolicy policy);
    // Detached UpdateApply op: download, verify, stage, then ApplyStatus. Done before the gate arms, never holding it.
    // A running one is returned and takes Now. updates.notify_only, updates.no_update, updates.busy.
    [[nodiscard]] Result<OpHandle> start_apply(ApplyWhen when);
    // Engine.drain{Update} from a client that already asked the user.
    // updates.no_update, updates.notify_only, updates.busy.
    [[nodiscard]] Result<void> drain_consented();

    // updates.below_min_supported, or updates.busy from the moment the gate opens.
    [[nodiscard]] Result<void> admit_new_session() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::updates
