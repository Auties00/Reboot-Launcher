#include "reboot/os_linux/platform/xdg_paths.hpp"

#include <filesystem>
#include <system_error>
#include <unistd.h>

#include "messages.hpp"
#include "passwd_entry.hpp"
#include "process_environment.hpp"
#include "reboot/posix/posix_error.hpp"
#include "xdg_dirs.hpp"

namespace rb::os_linux::platform {

namespace {

constexpr std::string_view kAppDir = "reboot-launcher";

}  // namespace

Result<XdgPaths> XdgPaths::detect() {
    const auto uid = static_cast<u32>(::geteuid());
    std::optional<PasswdEntry> entry = read_passwd(uid);
    if (!entry) return make_diag(ErrorDomain::Platform, kNoHome).arg("uid", uid).fail();

    XdgPaths paths;
    paths.uid_ = uid;
    paths.user_name_ = std::move(entry->name);
    paths.home_ = std::move(entry->home);
    paths.config_home_ = xdg_base(env_value("XDG_CONFIG_HOME"), paths.home_, ".config");
    paths.data_home_ = xdg_base(env_value("XDG_DATA_HOME"), paths.home_, ".local/share");
    paths.cache_home_ = xdg_base(env_value("XDG_CACHE_HOME"), paths.home_, ".cache");
    paths.state_home_ = xdg_base(env_value("XDG_STATE_HOME"), paths.home_, ".local/state");
    if (const auto runtime = env_value("XDG_RUNTIME_DIR"); runtime && runtime->starts_with('/'))
        paths.runtime_dir_ = NativePath{*runtime}.lexically_normal();

    const NativePath self{"/proc/self/exe"};
    std::error_code error;
    const NativePath exe = std::filesystem::read_symlink(self, error);
    if (error) return std::unexpected(posix::call_failed("readlink", error.value(), self));
    paths.exe_dir_ = without_deleted_suffix(exe).parent_path().lexically_normal();

    InstallFacts facts = detect_install(paths.exe_dir_, env_value("APPIMAGE"), env_value("APPDIR"));
    paths.appimage_ = std::move(facts.appimage);
    paths.tarball_root_ = std::move(facts.tarball_root);
    return paths;
}

NativePath XdgPaths::default_data_root() const { return data_home_ / kAppDir; }

NativePath XdgPaths::default_cache_root() const { return cache_home_ / kAppDir; }

NativePath XdgPaths::default_logs_root() const { return state_home_ / kAppDir / "logs"; }

NativePath XdgPaths::ipc_runtime_base() const {
    if (runtime_dir_) return *runtime_dir_;
    return runtime_base_for(std::nullopt, uid_);
}

NativePath XdgPaths::exe_dir() const { return exe_dir_; }

ports::InstallKind XdgPaths::install_kind() const {
    if (appimage_) return ports::InstallKind::AppImage;
    if (tarball_root_) return ports::InstallKind::Tarball;
    return ports::InstallKind::Dev;
}

}  // namespace rb::os_linux::platform
