#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace rb::os_linux::platform {

// An archive entry's path with "./" and empty components dropped; nullopt when absolute or
// holding "..". An empty result is the archive's root entry itself.
[[nodiscard]] std::optional<std::string> safe_entry_path(std::string_view path);

// The first component of a safe entry path.
[[nodiscard]] std::string_view top_component(std::string_view safe_path) noexcept;

// Whether a symlink at the safe path `entry` whose target is `target` stays inside the extraction
// root when resolved lexically; absolute targets never do.
[[nodiscard]] bool link_stays_inside(std::string_view entry, std::string_view target);

}  // namespace rb::os_linux::platform
