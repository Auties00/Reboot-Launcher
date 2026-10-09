#include "velopack_layout.hpp"

#include "reboot/foundation/text.hpp"
#include "wide.hpp"

namespace rb::os_windows::platform {

std::optional<NativePath> velopack_root_of(const NativePath& exe_dir, UniqueFunction<bool(const NativePath&)> file_exists) {
    const NativePath dir = exe_dir.has_filename() ? exe_dir : exe_dir.parent_path();
    if (!iequals_ascii(narrow(dir.filename().native()), "current")) return std::nullopt;
    NativePath root = dir.parent_path();
    if (root.empty() || root == dir) return std::nullopt;
    if (!file_exists(root / "Update.exe") || !file_exists(dir / "sq.version")) return std::nullopt;
    return root;
}

}  // namespace rb::os_windows::platform
