#include "reboot/os_macos/ipc/mac_client_paths.hpp"

#include "unistd.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <pwd.h>
#include <sys/types.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "client_path_rules.hpp"
#include "darwin_user_temp_dir.hpp"
#include "messages.hpp"
#include "reboot/posix/posix_error.hpp"

namespace rb::os_macos::ipc {
namespace {

constexpr std::string_view kAppDirName = "Reboot Launcher";
constexpr std::size_t kDefaultPasswdBuffer = 16384;
constexpr std::size_t kMaxPasswdBuffer = std::size_t{1} << 20;

// Any object of this image: dladdr names the file it was loaded from.
const char kImageAnchor = 0;

struct FreeDeleter {
    void operator()(char* text) const noexcept { std::free(text); }
};

[[nodiscard]] Result<NativePath> read_home(u32 uid) {
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
        return NativePath{entry.pw_dir};
    }
}

[[nodiscard]] std::optional<std::string> executable_path() {
    std::uint32_t size = 0;
    ::_NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    if (size == 0 || ::_NSGetExecutablePath(path.data(), &size) != 0) return std::nullopt;
    path.resize(std::strlen(path.c_str()));
    return path;
}

// The canonical path of the image holding this code.
[[nodiscard]] Result<NativePath> image_path() {
    Dl_info info{};
    if (::dladdr(&kImageAnchor, &info) == 0 || info.dli_fname == nullptr)
        return make_diag(ErrorDomain::Platform, kImageUnresolved).fail();
    std::string name = info.dli_fname;
    // dyld records the main executable as exec'd, which may be relative to a cwd since left.
    if (!name.starts_with('/')) {
        std::optional<std::string> executable = executable_path();
        if (!executable) return make_diag(ErrorDomain::Platform, kImageUnresolved).fail();
        name = std::move(*executable);
    }
    const std::unique_ptr<char, FreeDeleter> resolved{::realpath(name.c_str(), nullptr)};
    if (!resolved) return make_diag(ErrorDomain::Platform, kImageUnresolved).os(posix::errno_error(errno)).fail();
    return NativePath{resolved.get()};
}

// SecTranslocateIsTranslocatedURL ships in Security.framework without a public declaration.
[[nodiscard]] bool is_translocated(const NativePath& path) {
    using IsTranslocatedUrl = Boolean (*)(CFURLRef, bool*, CFErrorRef*);
    void* const symbol = ::dlsym(RTLD_DEFAULT, "SecTranslocateIsTranslocatedURL");
    if (symbol == nullptr) return false;
    const auto is_translocated_url = reinterpret_cast<IsTranslocatedUrl>(symbol);
    const std::string& native = path.native();
    const CFURLRef url = ::CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(native.data()), static_cast<CFIndex>(native.size()), true);
    if (url == nullptr) return false;
    bool translocated = false;
    const Boolean answered = is_translocated_url(url, &translocated, nullptr);
    ::CFRelease(url);
    return answered && translocated;
}

}  // namespace

MacClientPaths::MacClientPaths(NativePath home, NativePath user_temp_dir, NativePath exe_dir,
                               std::optional<NativePath> app_bundle, bool translocated)
    : home_(std::move(home)),
      user_temp_dir_(std::move(user_temp_dir)),
      exe_dir_(std::move(exe_dir)),
      app_bundle_(std::move(app_bundle)),
      translocated_(translocated) {}

Result<MacClientPaths> MacClientPaths::detect() {
    Result<NativePath> home = read_home(static_cast<u32>(::geteuid()));
    if (!home) return std::unexpected(std::move(home.error()));
    Result<NativePath> user_temp_dir = darwin_user_temp_dir();
    if (!user_temp_dir) return std::unexpected(std::move(user_temp_dir.error()));
    Result<NativePath> image = image_path();
    if (!image) return std::unexpected(std::move(image.error()));
    ImagePlacement placement = place_image(*image);
    const bool translocated = placement.app_bundle && is_translocated(*placement.app_bundle);
    return MacClientPaths{std::move(*home), std::move(*user_temp_dir), std::move(placement.exe_dir),
                          std::move(placement.app_bundle), translocated};
}

NativePath MacClientPaths::default_data_root() const {
    return home_ / "Library" / "Application Support" / kAppDirName;
}

NativePath MacClientPaths::default_cache_root() const { return home_ / "Library" / "Caches" / kAppDirName; }

NativePath MacClientPaths::default_logs_root() const { return home_ / "Library" / "Logs" / kAppDirName; }

NativePath MacClientPaths::ipc_runtime_base() const { return user_temp_dir_; }

NativePath MacClientPaths::exe_dir() const { return exe_dir_; }

ports::InstallKind MacClientPaths::install_kind() const {
    return app_bundle_ ? ports::InstallKind::AppBundle : ports::InstallKind::Dev;
}

std::optional<NativePath> MacClientPaths::velopack_package_dir() const {
    if (translocated_) return std::nullopt;
    return app_bundle_;
}

}  // namespace rb::os_macos::ipc
