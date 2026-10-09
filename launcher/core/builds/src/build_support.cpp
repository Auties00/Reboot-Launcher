#include "build_support.hpp"

#include <array>
#include <filesystem>
#include <system_error>

#include "builds_error.hpp"

namespace reboot::builds {

namespace {

constexpr std::array<std::string_view, 5> kSourceNames{"pe_resource", "cl_table", "raw_scan", "catalog", "user"};

[[nodiscard]] constexpr bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

}  // namespace

std::string_view version_source_name(VersionSource source) noexcept {
    return kSourceNames[static_cast<std::size_t>(source)];
}

std::optional<VersionSource> version_source_from_name(std::string_view name) noexcept {
    for (std::size_t i = 0; i < kSourceNames.size(); ++i)
        if (kSourceNames[i] == name) return static_cast<VersionSource>(i);
    return std::nullopt;
}

storage::LibraryLayout to_stored_layout(const BuildLayout& layout) {
    return storage::LibraryLayout{.shipping_exe = layout.shipping_exe,
                                  .launcher_exe = layout.launcher_exe,
                                  .eac_exe = layout.eac_exe,
                                  .crash_report_clients = layout.crash_report_clients,
                                  .aftermath_dlls = layout.aftermath_dlls};
}

BuildLayout from_stored_layout(const NativePath& root, const storage::LibraryLayout& layout) {
    return BuildLayout{.root = root,
                       .shipping_exe = layout.shipping_exe,
                       .launcher_exe = layout.launcher_exe,
                       .eac_exe = layout.eac_exe,
                       .crash_report_clients = layout.crash_report_clients,
                       .aftermath_dlls = layout.aftermath_dlls};
}

NativePath normal_root(const NativePath& path) {
    NativePath normal = path.lexically_normal();
    if (!normal.has_filename() && normal.has_relative_path()) normal = normal.parent_path();
    return normal;
}

std::string trim_ascii(std::string_view text) {
    while (!text.empty() && is_space(text.front())) text.remove_prefix(1);
    while (!text.empty() && is_space(text.back())) text.remove_suffix(1);
    return std::string(text);
}

Result<NativePath> canonical_root(const NativePath& root, const std::vector<InstalledBuild>& others) {
    std::error_code error;
    NativePath canonical = std::filesystem::weakly_canonical(root, error);
    if (error) {
        return std::unexpected(to_diagnostic(
            BuildsError{.code = BuildsErrorCode::Io, .path = root, .os_error = SystemError{.code = error.value()}}));
    }
    canonical = normal_root(canonical);
    for (const InstalledBuild& other : others) {
        std::error_code ignored;
        if (std::filesystem::equivalent(canonical, other.root, ignored)) {
            return std::unexpected(to_diagnostic(
                BuildsError{.code = BuildsErrorCode::AlreadyRegistered, .path = root, .name = other.name}));
        }
    }
    return canonical;
}

}  // namespace reboot::builds
