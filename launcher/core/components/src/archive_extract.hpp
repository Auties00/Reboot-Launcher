#pragma once

#include <expected>
#include <string>

#include "reboot/base/function.hpp"
#include "reboot/base/native_path.hpp"
#include "reboot/base/types.hpp"
#include "reboot/foundation/cancel.hpp"

namespace rb::components {

struct ExtractError {
    bool cancelled = false;
    std::string detail;
};

// Blocking. Unpacks every entry of `archive` under the existing directory `dest`, refusing absolute
// paths, ".." and writes through a symlink; returns the unpacked bytes. `on_progress` receives the
// archive bytes read so far.
[[nodiscard]] std::expected<u64, ExtractError> extract_archive(const NativePath& archive, const NativePath& dest,
                                                               const CancelToken& token,
                                                               UniqueFunction<void(u64 archive_bytes_read)> on_progress);

}  // namespace rb::components
