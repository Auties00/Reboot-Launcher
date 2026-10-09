#pragma once

#include <optional>

#include "reboot/catalog/catalog_entry.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::builds {

class IByteSource;

struct ByteWindow {
    u64 offset = 0;
    u64 length = 0;

    bool operator==(const ByteWindow&) const = default;
};

struct ArchiveProbe {
    // The format that is decoded: SevenZip for a ZIP-wrapped 7z.
    catalog::ArchiveFormat format = catalog::ArchiveFormat::Unrecognized;
    catalog::ArchiveContainer container = catalog::ArchiveContainer::None;
    // ZipStored: the inner 7z's bytes inside the file, read in place through this window.
    std::optional<ByteWindow> window;

    bool operator==(const ArchiveProbe&) const = default;
};

// Signatures only, never the extension: some host ".rar" files are ZIPs.
// Fails with builds.unsupported_archive or builds.corrupt_archive.
[[nodiscard]] Result<ArchiveProbe> probe_archive(IByteSource& source);

}  // namespace rb::builds
