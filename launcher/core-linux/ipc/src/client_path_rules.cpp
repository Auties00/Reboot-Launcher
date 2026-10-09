#include "client_path_rules.hpp"

#include "reboot/foundation/paths.hpp"

namespace reboot::os_linux::ipc {

NativePath xdg_home(std::optional<std::string_view> value, const NativePath& fallback) {
    if (value && value->starts_with('/')) return NativePath{*value}.lexically_normal();
    return fallback.lexically_normal();
}

std::optional<NativePath> tarball_root_of(const NativePath& exe_dir) {
    const NativePath versions = exe_dir.parent_path();
    if (!exe_dir.has_filename() || versions.filename() != "versions") return std::nullopt;
    const NativePath root = versions.parent_path();
    if (root.empty()) return std::nullopt;
    return root;
}

ports::InstallKind classify_install(const InstallFacts& facts) {
    const bool in_appdir =
        facts.appdir && facts.appdir->starts_with('/') && is_inside(facts.exe_dir, NativePath{*facts.appdir});
    if (facts.appimage && in_appdir) return ports::InstallKind::AppImage;
    if (facts.tarball_current_is_link && tarball_root_of(facts.exe_dir)) return ports::InstallKind::Tarball;
    return ports::InstallKind::Dev;
}

}  // namespace reboot::os_linux::ipc
