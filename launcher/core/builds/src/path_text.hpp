#pragma once

#include <string>

#include "reboot/foundation/native_path.hpp"

namespace rb::builds {

// The path's generic form as UTF-8 bytes, which never throws the way string() can on Windows.
[[nodiscard]] std::string utf8_name(const NativePath& path);

// utf8_name with ASCII letters lowered: two paths that differ only in ASCII case share it.
[[nodiscard]] std::string folded_key(const NativePath& path);

}  // namespace rb::builds
