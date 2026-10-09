#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/builds/build_layout.hpp"
#include "reboot/builds/installed_build.hpp"
#include "reboot/builds/version_source.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/storage/library_document.hpp"

namespace rb::builds {

[[nodiscard]] std::string_view version_source_name(VersionSource source) noexcept;
[[nodiscard]] std::optional<VersionSource> version_source_from_name(std::string_view name) noexcept;

[[nodiscard]] storage::LibraryLayout to_stored_layout(const BuildLayout& layout);
[[nodiscard]] BuildLayout from_stored_layout(const NativePath& root, const storage::LibraryLayout& layout);

// Lexically normal, without a trailing separator.
[[nodiscard]] NativePath normal_root(const NativePath& path);

// Without leading and trailing ASCII whitespace.
[[nodiscard]] std::string trim_ascii(std::string_view text);

// Blocking. The canonical form of `root`, refused as builds.already_registered when it is the same
// directory, by path or file id, as one of `others`' roots.
[[nodiscard]] Result<NativePath> canonical_root(const NativePath& root, const std::vector<InstalledBuild>& others);

}  // namespace rb::builds
