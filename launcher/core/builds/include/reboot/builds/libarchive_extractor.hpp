#pragma once

#include "reboot/builds/archive_extractor.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"

namespace rb::builds {

// Capabilities: game-builds.extract, game-builds.+4.
// In-process libarchive 3.8; a ZipStored 7z is read through the probe's window, never copied out.
// Only files and folders are written, by this code: "..", absolute, ADS and DOS-device names and
// link entries are refused with builds.unsafe_entry_path, so nothing lands outside the destination.
class LibArchiveExtractor final : public IArchiveExtractor {
public:
    Result<ExtractSummary> extract(const ExtractRequest& request, const CancelToken& token,
                                   UniqueFunction<void(const ExtractProgress&)> on_progress) override;
};

}  // namespace rb::builds
