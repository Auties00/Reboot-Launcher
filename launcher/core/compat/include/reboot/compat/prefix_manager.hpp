#pragma once

#include <memory>

#include "reboot/compat/compat_document.hpp"
#include "reboot/compat/prefix_lease.hpp"
#include "reboot/compat/prefix_request.hpp"
#include "reboot/compat/prepared_prefix.hpp"
#include "reboot/compat/runner_profile.hpp"
#include "reboot/components/progress_sink.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/storage/document_store.hpp"

namespace reboot {
class Executor;
class TimerService;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class IFileSystem;
class IProcessLauncher;
class IRunnerPlatform;
}  // namespace reboot::ports

namespace reboot::compat {

struct PrefixManagerDeps {
    ports::IProcessLauncher& processes;
    ports::IRunnerPlatform& runner;
    ports::IFileSystem& fs;
    WorkerPool& workers;
    Executor& strand;
    TimerService& timers;
    storage::DocumentStore<CompatDocument>& document;
};

// Covers no capability ids (decisions linux-compat-layer, macos-compat-layer, update-mechanism,
// persistence-format-migration).
// Strand-only. One prefix per Wine runner at data/prefixes/<runner_name>; Native has none and is
// refused with compat.no_prefix. Every prefix command goes through IRunnerPlatform::prefix_command,
// so under Umu umu and Proton create and upgrade the prefix (createprefix), never raw wineboot.
// prepare() brings the prefix to the session's runtime, one prepare per prefix at a time:
// - missing or unusable (no drive_c or system.reg): created; FortniteGame/Saved moves over from
//   the old tree's AppData/Local, then the old tree is removed;
// - a newer runtime version, or another runtime id at an equal version: upgraded in place;
// - an older version: copied to <runner_name>.backup-<recorded version>, replacing the runner's
//   previous backup, then upgraded in place;
// - VC++ seeded once, when a game DLL or a DLL beside it that one imports needs_vc_runtime; the
//   installer runs as a prefix command, which under Umu exposes its directory to pressure-vessel;
// - MacRuntime: DXMT's builtin DLLs never get a native override, and PreferredRHI is seeded.
// A change, and the server kill before a backup or recreation, needs `lease` to be the prefix's
// only lease, else PrefixBusy; a prefix that needs no change is ready whatever else uses it.
class PrefixManager {
public:
    PrefixManager(PrefixManagerDeps deps, const AppLayout& layout);
    ~PrefixManager();
    PrefixManager(const PrefixManager&) = delete;
    PrefixManager& operator=(const PrefixManager&) = delete;

    [[nodiscard]] Result<NativePath> prefix_dir(RunnerKind kind) const;

    // Taken at runtime preflight, before prepare(), so runtime setup sees the prefix in use.
    [[nodiscard]] Result<PrefixLease> lease(RunnerKind kind, SessionId session);
    // No lease and no prepare running.
    [[nodiscard]] bool idle(RunnerKind kind) const;

    void prepare(const PrefixLease& lease, PrefixRequest request, CancelToken token,
                 components::ProgressSink progress, UniqueFunction<void(Result<PreparedPrefix>)> done);

private:
    friend class PrefixLease;
    void release(u64 lease_id) noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::compat
