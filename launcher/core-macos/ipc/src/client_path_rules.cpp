#include "client_path_rules.hpp"

namespace rb::os_macos::ipc {
namespace {

[[nodiscard]] bool is_app_dir(const NativePath& dir) { return dir.extension() == ".app" && !dir.stem().empty(); }

// The image lies under <bundle>/Contents/MacOS or <bundle>/Contents/Frameworks.
[[nodiscard]] bool under_code_dir(const NativePath& bundle, const NativePath& image) {
    const NativePath relative = image.lexically_relative(bundle);
    auto part = relative.begin();
    if (part == relative.end() || *part != "Contents") return false;
    if (++part == relative.end() || (*part != "MacOS" && *part != "Frameworks")) return false;
    return ++part != relative.end();
}

}  // namespace

ImagePlacement place_image(const NativePath& image) {
    for (NativePath dir = image.parent_path(); dir.has_relative_path(); dir = dir.parent_path()) {
        if (!is_app_dir(dir)) continue;
        if (!under_code_dir(dir, image)) break;
        return {.app_bundle = dir, .exe_dir = dir / "Contents" / "MacOS"};
    }
    return {.app_bundle = std::nullopt, .exe_dir = image.parent_path()};
}

}  // namespace rb::os_macos::ipc
