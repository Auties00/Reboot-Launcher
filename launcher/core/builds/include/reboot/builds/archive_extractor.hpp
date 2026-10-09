#pragma once

#include <optional>
#include <string>
#include <vector>

#include "reboot/builds/archive_probe.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::builds {

struct ExtractRequest {
    NativePath archive;
    // An existing, empty directory on the archive's volume.
    NativePath destination;
};

// The total is known only for ZIP, whose central directory lists every size before any data.
struct ExtractProgress {
    u64 entries_done = 0;
    u64 bytes_done = 0;
    std::optional<u64> bytes_total;
};

// The later entry replaced the earlier, as it would on a case-insensitive Windows volume.
struct CaseCollision {
    std::string kept;
    std::string replaced;
};

struct ExtractSummary {
    ArchiveProbe probe;
    u64 entries = 0;
    u64 bytes = 0;
    std::vector<CaseCollision> case_collisions;
};

// Capabilities: game-builds.extract, game-builds.+4.
// Blocking; runs on the WorkerPool. The returned value is the only completion signal.
class IArchiveExtractor {
public:
    virtual ~IArchiveExtractor() = default;

    // `on_progress` runs on the calling thread per data block, so one multi-GiB entry keeps an
    // Install op's liveness deadline armed; the token is checked at the same points.
    virtual Result<ExtractSummary> extract(const ExtractRequest& request, const CancelToken& token,
                                           UniqueFunction<void(const ExtractProgress&)> on_progress) = 0;
};

}  // namespace rb::builds
