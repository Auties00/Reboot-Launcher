#include "xdg_dirs.hpp"

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include "reboot/foundation/paths.hpp"

namespace reboot::os_linux::platform {

namespace {

constexpr std::string_view kDeletedSuffix = " (deleted)";

[[nodiscard]] bool absolute_value(std::optional<std::string_view> value) noexcept {
    return value && value->starts_with('/');
}

}  // namespace

NativePath xdg_base(std::optional<std::string_view> value, const NativePath& home, std::string_view fallback) {
    if (absolute_value(value)) return NativePath{*value}.lexically_normal();
    return (home / fallback).lexically_normal();
}

NativePath runtime_base_for(std::optional<std::string_view> xdg_runtime_dir, u32 uid) {
    if (absolute_value(xdg_runtime_dir)) return NativePath{*xdg_runtime_dir}.lexically_normal();
    return NativePath{"/tmp/reboot-launcher-" + std::to_string(uid)};
}

InstallFacts detect_install(const NativePath& exe_dir, std::optional<std::string_view> appimage,
                            std::optional<std::string_view> appdir) {
    namespace fs = std::filesystem;
    std::error_code error;
    if (absolute_value(appimage) && absolute_value(appdir) && fs::is_regular_file(NativePath{*appimage}, error) &&
        is_inside(exe_dir, NativePath{*appdir}))
        return {.kind = ports::InstallKind::AppImage, .appimage = NativePath{*appimage}.lexically_normal()};

    const NativePath versions = exe_dir.parent_path();
    if (exe_dir.has_filename() && versions.filename() == "versions") {
        const NativePath root = versions.parent_path();
        if (fs::is_symlink(root / "current", error)) return {.kind = ports::InstallKind::Tarball, .tarball_root = root};
    }
    return {};
}

NativePath without_deleted_suffix(NativePath exe) {
    std::string text = exe.string();
    if (!text.ends_with(kDeletedSuffix)) return exe;
    text.resize(text.size() - kDeletedSuffix.size());
    return NativePath{std::move(text)};
}

}  // namespace reboot::os_linux::platform
