#pragma once

#include <span>
#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::compat {

// Covers no capability ids (decision linux-compat-layer).
// The DLL names a PE32+ image imports, regular and delay-loaded, as the image spells them.
// compat.pe_malformed naming `path` for a truncated image or a table outside it; never reads
// past `image`.
[[nodiscard]] Result<std::vector<std::string>> read_pe_imports(const NativePath& path, std::span<const u8> image);

// True when an import comes from the MSVC C++ runtime (msvcp140*, vcruntime140*), compared
// case-insensitively: the prefix then needs the VC++ redistributable.
[[nodiscard]] bool needs_vc_runtime(std::span<const std::string> imports) noexcept;

}  // namespace rb::compat
