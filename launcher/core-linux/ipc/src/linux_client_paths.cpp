#include "reboot/os_linux/ipc/linux_client_paths.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <dlfcn.h>
#include <link.h>
#include <memory>
#include <pwd.h>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "client_path_rules.hpp"
#include "messages.hpp"
#include "reboot/posix/posix_error.hpp"

namespace rb::os_linux::ipc {
namespace {

constexpr std::string_view kAppDirName = "reboot-launcher";
constexpr std::size_t kDefaultPasswdBuffer = 16384;
constexpr std::size_t kMaxPasswdBuffer = std::size_t{1} << 20;

// Any object of this image: dladdr names the file it was loaded from.
const char kImageAnchor = 0;

struct FreeDeleter {
    void operator()(char* text) const noexcept { std::free(text); }
};

struct PasswdEntry {
    NativePath home;
    std::string name;
    std::string shell;
};

[[nodiscard]] std::optional<std::string_view> environment_value(const char* name) {
    const char* const value = std::getenv(name);
    if (value == nullptr) return std::nullopt;
    return std::string_view{value};
}

[[nodiscard]] Result<PasswdEntry> read_passwd(u32 uid) {
    const long suggested = ::sysconf(_SC_GETPW_R_SIZE_MAX);
    std::size_t size = suggested > 0 ? static_cast<std::size_t>(suggested) : kDefaultPasswdBuffer;
    for (;;) {
        std::vector<char> buffer(size);
        passwd entry{};
        passwd* found = nullptr;
        const int error = ::getpwuid_r(static_cast<uid_t>(uid), &entry, buffer.data(), buffer.size(), &found);
        if (error == EINTR) continue;
        if (error == ERANGE && size < kMaxPasswdBuffer) {
            size *= 2;
            continue;
        }
        if (error != 0 || found == nullptr || entry.pw_dir == nullptr || entry.pw_dir[0] != '/') {
            DiagBuilder diag = make_diag(ErrorDomain::Platform, kHomeUnavailable).arg("uid", uid);
            if (error != 0) return std::move(diag).os(posix::errno_error(error)).fail();
            return std::move(diag).fail();
        }
        const bool has_shell = entry.pw_shell != nullptr && entry.pw_shell[0] != '\0';
        return PasswdEntry{NativePath{entry.pw_dir}, entry.pw_name != nullptr ? entry.pw_name : "",
                           has_shell ? entry.pw_shell : "/bin/sh"};
    }
}

// The canonical path of the image holding this code.
[[nodiscard]] Result<NativePath> image_path() {
    Dl_info info{};
    void* extra = nullptr;
    if (::dladdr1(&kImageAnchor, &info, &extra, RTLD_DL_LINKMAP) == 0)
        return make_diag(ErrorDomain::Platform, kImageUnresolved).fail();
    const auto* map = static_cast<const link_map*>(extra);
    // The main program's link map has an empty name, for which dladdr reports argv[0].
    const char* const name =
        map != nullptr && map->l_name != nullptr && map->l_name[0] != '\0' ? map->l_name : "/proc/self/exe";
    const std::unique_ptr<char, FreeDeleter> resolved{::realpath(name, nullptr)};
    if (!resolved) return make_diag(ErrorDomain::Platform, kImageUnresolved).os(posix::errno_error(errno)).fail();
    return NativePath{resolved.get()};
}

[[nodiscard]] std::optional<NativePath> appimage_file() {
    const std::optional<std::string_view> appimage = environment_value("APPIMAGE");
    if (!appimage || !appimage->starts_with('/')) return std::nullopt;
    const NativePath path{*appimage};
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) return std::nullopt;
    return path;
}

[[nodiscard]] bool names_symlink(const NativePath& path) {
    struct stat info {};
    return ::lstat(path.c_str(), &info) == 0 && S_ISLNK(info.st_mode);
}

}  // namespace

Result<LinuxClientPaths> LinuxClientPaths::detect() {
    LinuxClientPaths paths;
    paths.uid_ = static_cast<u32>(::geteuid());
    Result<PasswdEntry> entry = read_passwd(paths.uid_);
    if (!entry) return std::unexpected(std::move(entry.error()));
    paths.home_ = std::move(entry->home);
    paths.user_name_ = std::move(entry->name);
    paths.login_shell_ = std::move(entry->shell);
    paths.data_home_ = xdg_home(environment_value("XDG_DATA_HOME"), paths.home_ / ".local" / "share");
    paths.cache_home_ = xdg_home(environment_value("XDG_CACHE_HOME"), paths.home_ / ".cache");
    paths.state_home_ = xdg_home(environment_value("XDG_STATE_HOME"), paths.home_ / ".local" / "state");
    paths.runtime_base_ = linux_ipc_runtime_base(paths.uid_);

    Result<NativePath> image = image_path();
    if (!image) return std::unexpected(std::move(image.error()));
    paths.exe_dir_ = image->parent_path();

    std::optional<NativePath> appimage = appimage_file();
    const std::optional<NativePath> tarball_root = tarball_root_of(paths.exe_dir_);
    paths.install_kind_ = classify_install({
        .exe_dir = paths.exe_dir_,
        .appimage = appimage,
        .appdir = environment_value("APPDIR"),
        .tarball_current_is_link = tarball_root && names_symlink(*tarball_root / "current"),
    });
    if (paths.install_kind_ == ports::InstallKind::AppImage) paths.appimage_ = std::move(appimage);
    return paths;
}

NativePath LinuxClientPaths::default_data_root() const { return data_home_ / kAppDirName; }

NativePath LinuxClientPaths::default_cache_root() const { return cache_home_ / kAppDirName; }

NativePath LinuxClientPaths::default_logs_root() const { return state_home_ / kAppDirName / "logs"; }

NativePath LinuxClientPaths::exe_dir() const { return exe_dir_; }

ports::InstallKind LinuxClientPaths::install_kind() const { return install_kind_; }

}  // namespace rb::os_linux::ipc
