#include "darwin.hpp"

#include "reboot/os_macos/platform/mac_paths.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <pwd.h>
#include <sys/types.h>

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "bundle_layout.hpp"
#include "messages.hpp"
#include "reboot/posix/posix_error.hpp"

namespace rb::os_macos::platform {

namespace {

constexpr std::string_view kAppDirName = "Reboot Launcher";

[[nodiscard]] Result<NativePath> home_directory() {
    const uid_t uid = ::getuid();
    const long suggested = ::sysconf(_SC_GETPW_R_SIZE_MAX);
    std::vector<char> buffer(suggested > 0 ? static_cast<std::size_t>(suggested) : 4096);
    for (;;) {
        passwd entry{};
        passwd* found = nullptr;
        const int error = ::getpwuid_r(uid, &entry, buffer.data(), buffer.size(), &found);
        if (error == ERANGE) {
            buffer.resize(buffer.size() * 2);
            continue;
        }
        if (error != 0 && error != EINTR)
            return std::unexpected(posix::call_failed("getpwuid_r", error));
        if (error == EINTR) continue;
        if (found == nullptr || found->pw_dir == nullptr || found->pw_dir[0] != '/')
            return make_diag(ErrorDomain::Platform, kNoHome).arg("uid", static_cast<u64>(uid)).fail();
        return NativePath{std::string(found->pw_dir)};
    }
}

[[nodiscard]] Result<NativePath> user_temp_dir() {
    errno = 0;
    const std::size_t size = ::confstr(_CS_DARWIN_USER_TEMP_DIR, nullptr, 0);
    if (size == 0) return std::unexpected(posix::call_failed("confstr", errno != 0 ? errno : ENOENT));
    std::string buffer(size, '\0');
    const std::size_t written = ::confstr(_CS_DARWIN_USER_TEMP_DIR, buffer.data(), buffer.size());
    if (written == 0 || written > size) return std::unexpected(posix::call_failed("confstr", errno != 0 ? errno : ENOENT));
    buffer.resize(written - 1);
    NativePath directory{std::move(buffer)};
    if (!directory.is_absolute()) return std::unexpected(posix::call_failed("confstr", EINVAL));
    return directory;
}

[[nodiscard]] Result<NativePath> executable_path() {
    std::uint32_t size = 0;
    (void)::_NSGetExecutablePath(nullptr, &size);
    std::string raw(size, '\0');
    if (::_NSGetExecutablePath(raw.data(), &size) != 0) return std::unexpected(posix::call_failed("_NSGetExecutablePath", ERANGE));
    raw.resize(std::char_traits<char>::length(raw.c_str()));
    std::unique_ptr<char, decltype(&std::free)> resolved{::realpath(raw.c_str(), nullptr), &std::free};
    if (!resolved) return std::unexpected(posix::call_failed("realpath", errno, NativePath{raw}));
    return NativePath{std::string(resolved.get())};
}

using IsTranslocatedFn = Boolean (*)(CFURLRef, bool*, CFErrorRef*);

// SecTranslocateIsTranslocatedURL is exported by Security but not declared in the public SDK.
[[nodiscard]] bool is_translocated(const NativePath& bundle) {
    void* security = ::dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY | RTLD_LOCAL);
    if (security == nullptr) return false;
    const auto check = reinterpret_cast<IsTranslocatedFn>(::dlsym(security, "SecTranslocateIsTranslocatedURL"));
    bool translocated = false;
    if (check != nullptr) {
        const std::string& raw = bundle.native();
        CFURLRef url = ::CFURLCreateFromFileSystemRepresentation(
            kCFAllocatorDefault, reinterpret_cast<const UInt8*>(raw.data()), static_cast<CFIndex>(raw.size()), true);
        if (url != nullptr) {
            if (!check(url, &translocated, nullptr)) translocated = false;
            ::CFRelease(url);
        }
    }
    ::dlclose(security);
    return translocated;
}

}  // namespace

MacPaths::MacPaths(NativePath home, NativePath user_temp_dir, NativePath exe_dir, std::optional<NativePath> app_bundle,
                   bool translocated)
    : home_(std::move(home)),
      user_temp_dir_(std::move(user_temp_dir)),
      exe_dir_(std::move(exe_dir)),
      app_bundle_(std::move(app_bundle)),
      translocated_(translocated) {}

Result<MacPaths> MacPaths::detect() {
    Result<NativePath> home = home_directory();
    if (!home) return std::unexpected(std::move(home.error()));
    Result<NativePath> temp = user_temp_dir();
    if (!temp) return std::unexpected(std::move(temp.error()));
    Result<NativePath> exe = executable_path();
    if (!exe) return std::unexpected(std::move(exe.error()));
    NativePath exe_dir = exe->parent_path();
    std::optional<NativePath> bundle = app_bundle_of(exe_dir);
    const bool translocated = bundle && is_translocated(*bundle);
    return MacPaths(std::move(*home), std::move(*temp), std::move(exe_dir), std::move(bundle), translocated);
}

NativePath MacPaths::default_data_root() const { return home_ / "Library" / "Application Support" / kAppDirName; }

NativePath MacPaths::default_cache_root() const { return home_ / "Library" / "Caches" / kAppDirName; }

NativePath MacPaths::default_logs_root() const { return home_ / "Library" / "Logs" / kAppDirName; }

NativePath MacPaths::ipc_runtime_base() const { return user_temp_dir_; }

NativePath MacPaths::exe_dir() const { return exe_dir_; }

ports::InstallKind MacPaths::install_kind() const {
    return app_bundle_ ? ports::InstallKind::AppBundle : ports::InstallKind::Dev;
}

std::optional<NativePath> MacPaths::velopack_package_dir() const {
    if (translocated_) return std::nullopt;
    return app_bundle_;
}

}  // namespace rb::os_macos::platform
