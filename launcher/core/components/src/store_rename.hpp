#pragma once

#include <system_error>

namespace rb::components {

// Sharing, lock and access-denied errors: another process (an antivirus scan, an indexer) holds the
// file or its directory for now.
[[nodiscard]] inline bool transient_rename_error(const std::error_code& error) noexcept {
    return error == std::errc::permission_denied || error == std::errc::no_lock_available ||
           error == std::errc::device_or_resource_busy || error == std::errc::text_file_busy;
}

}  // namespace rb::components
