#pragma once

#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::builds {

class IByteSource;

struct ZipEntry {
    std::string name;
    u16 flags = 0;
    u16 method = 0;
    u64 compressed_size = 0;
    u64 uncompressed_size = 0;
    u64 local_header_offset = 0;
};

// The central directory, ZIP64 included. Fails with builds.corrupt_archive (no path; callers add it).
[[nodiscard]] Result<std::vector<ZipEntry>> read_zip_directory(IByteSource& source);

// Where the entry's data starts, past its local header.
[[nodiscard]] Result<u64> zip_data_offset(IByteSource& source, const ZipEntry& entry);

}  // namespace reboot::builds
