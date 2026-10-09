#include "bundle_layout.hpp"

namespace reboot::os_macos::platform {

std::optional<NativePath> app_bundle_of(const NativePath& exe_dir) {
    NativePath dir = exe_dir.lexically_normal();
    if (!dir.has_filename()) dir = dir.parent_path();
    if (dir.filename() != "MacOS") return std::nullopt;
    const NativePath contents = dir.parent_path();
    if (contents.filename() != "Contents") return std::nullopt;
    NativePath bundle = contents.parent_path();
    if (bundle.extension() != ".app" || bundle.stem().empty()) return std::nullopt;
    return bundle;
}

}  // namespace reboot::os_macos::platform
