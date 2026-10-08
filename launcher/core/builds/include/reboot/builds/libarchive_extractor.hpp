#pragma once

#include "reboot/builds/archive_extractor.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"

namespace reboot::builds {

// Capabilities: game-builds.extract, game-builds.+4.
// In-process libarchive 3.8; a ZipStored 7z is read through the probe's window, never copied out.
// SECURE_NODOTDOT, SECURE_SYMLINKS and SECURE_NOABSOLUTEPATHS apply; ADS and DOS-device names are
// refused as well, with builds.unsafe_entry_path.
class LibArchiveExtractor final : public IArchiveExtractor {
public:
    Result<ExtractSummary> extract(const ExtractRequest& request, const CancelToken& token,
                                   UniqueFunction<void(const ExtractProgress&)> on_progress) override;
};

}  // namespace reboot::builds
