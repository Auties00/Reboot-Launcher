#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "reboot/components/component_info.hpp"
#include "reboot/components/component_pin.hpp"
#include "reboot/components/component_ref.hpp"
#include "reboot/components/integrity_hold.hpp"
#include "reboot/components/payload_set.hpp"
#include "reboot/components/pinned_payload.hpp"
#include "reboot/components/pinned_runtime.hpp"
#include "reboot/components/progress_sink.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot {
class EventBus;
class Executor;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class IFileSystem;
class IFileWatcher;
class ISecurityProductProbe;
}  // namespace reboot::ports

namespace reboot::net {
class ResumableDownloader;
}

namespace reboot::components {

class ManifestService;

struct ComponentStoreDeps {
    ports::IFileSystem& fs;
    ports::IFileWatcher& watcher;
    ports::ISecurityProductProbe& security;
    net::ResumableDownloader& downloader;
    WorkerPool& workers;
    Executor& strand;
    OpRegistry& ops;
    EventBus& events;
    ManifestService& manifest;
};

// Capabilities: dll-injection.dependency-download, dll-injection.prelaunch-verification, os-integration.antivirus-detection.
// Strand-only; file work, hashing, extraction and the security probe run on the WorkerPool.
// - Content-addressed under data/components: payload/<sha256>/<file name>, runtime/<sha256>/,
//   .staging/ for work in flight and index.json for the stored and last good versions. A stored
//   file never changes in place.
// - data/components, .staging and every version directory are created with
//   IFileSystem::create_dirs_owner_only.
// - A download resumes in staging. The downloader enforces the manifest size
//   (net.download_size_mismatch); the store then checks sha256 (components.checksum_mismatch)
//   and renames the file into place.
// - A staged file that vanishes or becomes unreadable between the end of the download and its hash
//   raises VanishedAfterDownload, not components.store_failed.
// - GC keeps a version that is pinned, selected by the manifest, or the last one that completed a
//   good session (N-1); everything else is removed.
// - A DeletionGuard report or a failed hold is re-verified first (exists, same size and sha256).
//   Only a file still missing, unreadable or mismatched is re-fetched and published as a
//   ComponentChangedEvent carrying a ComponentProblem; a file in use raises none.
// - ISecurityProductProbe is asked only for a missing or access-denied file.
class ComponentStore {
public:
    ComponentStore(ComponentStoreDeps deps, const AppLayout& layout);
    ~ComponentStore();
    ComponentStore(const ComponentStore&) = delete;
    ComponentStore& operator=(const ComponentStore&) = delete;

    // Startup: reads index.json, checks every recorded file, clears staging and tracks every
    // stored file.
    void load(UniqueFunction<void(Result<void>)> done);

    [[nodiscard]] std::vector<ComponentInfo> list() const;

    // Components.ensure: fetches the version the manifest selects; completes with its ComponentRef.
    [[nodiscard]] Result<OpHandle> start_ensure(std::string_view component_id, DisconnectPolicy policy);
    // Components.remove: removes every unpinned version; components.pinned while a session uses one.
    [[nodiscard]] Result<OpHandle> start_remove(std::string_view component_id, DisconnectPolicy policy);

    // Play preflight: the selected payload's required_payload_roles() files for this platform,
    // fetched when absent and re-hashed, pinned to `session`. components.no_payload when the
    // manifest has none for this payload_abi.
    void acquire_payload(SessionId session, CancelToken token, ProgressSink progress,
                         UniqueFunction<void(Result<PinnedPayload>)> done);
    // components.runtime_wrong_platform for a runtime runtime_matches() refuses.
    void acquire_runtime(SessionId session, std::string_view runtime_id, CancelToken token, ProgressSink progress,
                         UniqueFunction<void(Result<PinnedRuntime>)> done);

    // Native runner (Windows) only, right before injection; under Wine winhost holds the files.
    // On a missing, access-denied or mismatched file it reports the problem, re-fetches with
    // `progress` and retries once; a file in use fails with components.file_in_use.
    void hold(const PayloadSet& payload, PayloadRole role, CancelToken token, ProgressSink progress,
              UniqueFunction<void(Result<IntegrityHold>)> done);

    // A session on the pinned version completed, so it becomes the last good version and the one
    // before it may be collected. Updates the index at once; the index.json write runs on the
    // WorkerPool and a failure is logged, since the next good session writes it again.
    void mark_good(const ComponentPin& pin);

private:
    friend class ComponentPin;
    void unpin(u64 pin_id) noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::components
