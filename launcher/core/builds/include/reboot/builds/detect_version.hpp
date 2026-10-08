#pragma once

#include <optional>

#include "reboot/builds/build_layout.hpp"
#include "reboot/builds/detected_version.hpp"
#include "reboot/builds/release_marker.hpp"
#include "reboot/builds/version_detection.hpp"
#include "reboot/builds/version_source.hpp"
#include "reboot/catalog/alias_table.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"

namespace reboot::builds {

class ClTable;
class PeVersionReader;

struct DetectionTables {
    // Immutable for the engine's lifetime.
    const ClTable& cl_table;
    // A copy: CatalogService replaces its table on refresh while a worker may still read this one.
    catalog::AliasTable aliases;
};

// Capabilities: game-builds.version-detection.
// Pure. "Cert"/"Next" go through the ClTable; any other tail loses "-CL-<n>" and goes through the
// aliases, then GameVersion::parse. `cl` is that suffix, else the engine changelist.
[[nodiscard]] std::optional<DetectedVersion> derive_version(const ReleaseMarker& marker, VersionSource source,
                                                            const DetectionTables& tables);

// Capabilities: game-builds.version-detection, game-builds.file-search.
// CrashReportClient.exe in rank order, then the shipping exe's RT_VERSION, then its raw scan.
// Blocking; runs on the WorkerPool. Fails only with builds.cancelled.
[[nodiscard]] Result<VersionDetection> detect_version(const BuildLayout& layout, const PeVersionReader& reader,
                                                      const DetectionTables& tables, const CancelToken& token);

}  // namespace reboot::builds
