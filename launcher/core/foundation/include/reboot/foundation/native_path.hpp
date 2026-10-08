#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "reboot/foundation/result_fwd.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot {

using NativePath = std::filesystem::path;

// A path that crosses a process boundary without loss: `native` is UTF-16LE on Windows and
// the raw bytes on POSIX; `display` is for messages only.
struct WirePath {
    std::string display;
    std::vector<u8> native;

    bool operator==(const WirePath&) const = default;
};

[[nodiscard]] WirePath to_wire(const NativePath& path);
[[nodiscard]] Result<NativePath> from_wire(const WirePath& path);

// Lossy; never feed the result back into a path.
[[nodiscard]] std::string display_utf8(const NativePath& path);

}  // namespace reboot
