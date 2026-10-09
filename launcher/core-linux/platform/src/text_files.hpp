#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace rb::os_linux::platform {

// Reads a whole small file, such as one under /proc, /sys or /etc, which may report size 0.
// Fails with posix.call_failed_on_path; content past `limit` is dropped.
[[nodiscard]] Result<std::string> read_text_file(const NativePath& path, std::size_t limit = std::size_t{16} << 20);

// read_text_file, with any failure as nullopt.
[[nodiscard]] std::optional<std::string> try_read_text_file(const NativePath& path);

}  // namespace rb::os_linux::platform
