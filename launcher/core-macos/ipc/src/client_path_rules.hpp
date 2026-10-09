#pragma once

#include <optional>

#include "reboot/foundation/native_path.hpp"

namespace rb::os_macos::ipc {

struct ImagePlacement {
    // The innermost <X>.app above the image, when the image sits under its Contents/MacOS or
    // Contents/Frameworks (a framework nests deeper, e.g. Frameworks/X.framework/Versions/A/X).
    std::optional<NativePath> app_bundle;
    // <X>.app/Contents/MacOS inside a bundle, the image's own directory otherwise.
    NativePath exe_dir;
};

// `image` is the canonical path of the image holding the client library.
[[nodiscard]] ImagePlacement place_image(const NativePath& image);

}  // namespace rb::os_macos::ipc
