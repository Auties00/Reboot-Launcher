#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/components/app_entry.hpp"
#include "reboot/components/endpoint_override.hpp"
#include "reboot/components/manifest_platform.hpp"
#include "reboot/components/payload_entry.hpp"
#include "reboot/components/release_manifest.hpp"
#include "reboot/components/runtime_entry.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace reboot {
class IClock;
class Executor;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class IFileSystem;
}

namespace reboot::net {
class HttpClient;
}

namespace reboot::trust {
class KeyRing;
class SerialGuard;
}  // namespace reboot::trust

namespace reboot::components {

enum class ManifestOrigin : u8 { Fetched, Cached, Bundled };

enum class ManifestRefresh : u8 { Adopted, Unchanged };

// Both URLs are https; the signature is the detached ".sig" that trust::make_signed_document reads.
struct ManifestOptions {
    std::string url;
    std::string signature_url;
    std::string channel;
    ManifestPlatform platform = build_platform();
};

struct ManifestServiceDeps {
    ports::IFileSystem& fs;
    net::HttpClient& http;
    WorkerPool& workers;
    Executor& strand;
    const IClock& clock;
    const trust::KeyRing& keys;
    trust::SerialGuard& serials;
};

// Capabilities: packaging-distribution.assets, dll-injection.dependency-download.
// Strand-only; file work runs on the WorkerPool.
// - The signature, then expiry, then serial gate a fetched manifest as a whole; a refused one
//   leaves the copy in use untouched. The version comparison gates only update_offer().
// - Expiry is checked before SerialGuard::admit, which persists a higher serial first: an expired
//   manifest must never raise the floor and lock out valid ones with lower serials.
// - An admitted manifest is written to cache/manifest.json, with its .sig beside it, before the
//   listeners run.
class ManifestService {
public:
    ManifestService(ManifestServiceDeps deps, ManifestOptions options, const AppLayout& layout,
                    const InstallLayout& install);
    ~ManifestService();
    ManifestService(const ManifestService&) = delete;
    ManifestService& operator=(const ManifestService&) = delete;

    // Startup: the cached copy, else the bundled snapshot, each with its signature re-verified.
    // The bundled snapshot ships inside the signed app, so it skips the serial gate.
    void load(UniqueFunction<void(Result<ManifestOrigin>)> done);

    // Fetches under the HttpSmall deadline. A manifest with the current serial is Unchanged.
    void refresh(CancelToken token, UniqueFunction<void(Result<ManifestRefresh>)> done);

    [[nodiscard]] const ReleaseManifest* current() const noexcept;
    [[nodiscard]] std::optional<ManifestOrigin> origin() const noexcept;
    // A Warning once the copy in use has expired; it stays in use until a fresh one is admitted.
    [[nodiscard]] std::optional<Diagnostic> expiry_warning() const;

    // The entry for this platform and channel.
    [[nodiscard]] std::optional<AppEntry> app_entry() const;
    [[nodiscard]] std::optional<AppEntry> update_offer(const SemVer& installed) const;
    [[nodiscard]] std::optional<EndpointOverride> endpoint_override() const;

    // The newest payload whose payload_abi matches and that carries every required_payload_roles()
    // file for this platform; components.no_payload when there is none.
    [[nodiscard]] Result<PayloadEntry> payload() const;
    // components.unknown_runtime, or components.runtime_wrong_platform unless runtime_matches().
    [[nodiscard]] Result<RuntimeEntry> runtime(std::string_view id) const;
    // The runtimes runtime_matches() accepts for this platform, VcRedist included off Windows.
    [[nodiscard]] std::vector<RuntimeEntry> runtimes() const;

    // Runs on the strand after each newly adopted manifest.
    void add_listener(UniqueFunction<void(const ReleaseManifest&)> on_changed);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::components
