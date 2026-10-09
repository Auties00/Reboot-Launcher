#include "reboot/os_macos/runner/dxmt_wine_runner_platform.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>

#include <cerrno>
#include <filesystem>
#include <system_error>
#include <utility>

#include "messages.hpp"
#include "reboot/os_macos/runner/mac_runtime_layout.hpp"
#include "reboot/posix/posix_error.hpp"

namespace rb::os_macos::runner {

namespace {

namespace fs = std::filesystem;

constexpr const char* kQuarantineAttribute = "com.apple.quarantine";
// Rosetta 2's translation daemon; it exists only once Rosetta is installed.
constexpr std::string_view kRosettaDaemon = "/Library/Apple/usr/libexec/oahd";

int remove_quarantine(const NativePath& path) {
    return ::removexattr(path.c_str(), kQuarantineAttribute, XATTR_NOFOLLOW) == 0 ? 0 : errno;
}

// Removing an xattr needs write access, so a read-only entry we own gets owner write meanwhile.
int remove_quarantine_read_only(const NativePath& path) {
    struct stat info {};
    if (::lstat(path.c_str(), &info) != 0) return errno;
    if (S_ISLNK(info.st_mode) || (info.st_mode & S_IWUSR) != 0) return EACCES;
    const auto mode = static_cast<mode_t>(info.st_mode & 07777);
    if (::fchmodat(AT_FDCWD, path.c_str(), static_cast<mode_t>(mode | S_IWUSR), AT_SYMLINK_NOFOLLOW) != 0)
        return EACCES;
    const int removed = remove_quarantine(path);
    if (::fchmodat(AT_FDCWD, path.c_str(), mode, AT_SYMLINK_NOFOLLOW) != 0 && (removed == 0 || removed == ENOATTR))
        return errno;
    return removed;
}

// ENOATTR: never set. ENOTSUP: the volume has no xattrs.
Result<void> strip_quarantine(const NativePath& path) {
    int error = remove_quarantine(path);
    if (error == EACCES) error = remove_quarantine_read_only(path);
    if (error == 0 || error == ENOATTR || error == ENOTSUP) return {};
    return make_diag(ErrorDomain::Platform, kQuarantineStripFailed)
        .arg("path", path)
        .os(posix::errno_error(error))
        .kind(error == ENOENT ? ErrorKind::NotFound : ErrorKind::Generic)
        .fail();
}

void set_prefix(ports::EnvBlock& env, const NativePath& prefix) {
    std::erase_if(env.vars, [](const auto& var) { return var.first == "WINEPREFIX"; });
    // string() is the native bytes on POSIX.
    env.vars.emplace_back("WINEPREFIX", prefix.string());
}

bool rosetta_installed() {
    std::error_code error;
    return fs::exists(NativePath(kRosettaDaemon), error);
}

}  // namespace

std::vector<ports::RunnerKind> DxmtWineRunnerPlatform::supported() const {
    if (cpu_ == HostCpu::AppleSilicon) return {ports::RunnerKind::MacRuntime};
    return {};
}

Result<ports::RuntimeLayout> DxmtWineRunnerPlatform::layout(ports::RunnerKind kind, const ports::RuntimeDirs& dirs) {
    if (kind != ports::RunnerKind::MacRuntime)
        return make_diag(ErrorDomain::Platform, kRunnerKindUnsupported)
            .arg("runner", kind)
            .kind(ErrorKind::Unsupported)
            .fail();
    if (cpu_ != HostCpu::AppleSilicon)
        return make_diag(ErrorDomain::Platform, kNeedsAppleSilicon).kind(ErrorKind::Unsupported).fail();
    auto resolved = MacRuntimeLayout::resolve(dirs.runtime);
    if (!resolved) return std::unexpected(std::move(resolved.error()));
    return resolved->to_runtime_layout();
}

Result<void> DxmtWineRunnerPlatform::post_extract(const NativePath& runtime_dir) {
    if (auto stripped = strip_quarantine(runtime_dir); !stripped) return stripped;
    std::error_code error;
    fs::recursive_directory_iterator entry(runtime_dir, error);
    for (; !error && entry != fs::recursive_directory_iterator(); entry.increment(error))
        if (auto stripped = strip_quarantine(entry->path()); !stripped) return stripped;
    if (error)
        return make_diag(ErrorDomain::Platform, kRuntimeReadFailed)
            .arg("path", runtime_dir)
            .os(posix::errno_error(error.value()))
            .fail();
    return {};
}

Result<ports::ProcessLaunch> DxmtWineRunnerPlatform::runner_launch(const ports::RuntimeLayout& layout,
                                                                   const NativePath& prefix,
                                                                   const NativePath& winhost_exe,
                                                                   ports::EnvBlock base) {
    set_prefix(base, prefix);

    ports::ProcessLaunch launch;
    launch.exe = layout.entry;
    launch.args = {winhost_exe.string()};
    launch.env = std::move(base);
    launch.cwd = winhost_exe.parent_path();
    launch.stdio = ports::StdioMode::Capture;
    launch.own_group = true;
    return launch;
}

Result<ports::ProcessLaunch> DxmtWineRunnerPlatform::prefix_command(const ports::RuntimeLayout& layout,
                                                                    const NativePath& prefix,
                                                                    const ports::PrefixCommand& command,
                                                                    ports::EnvBlock base) {
    set_prefix(base, prefix);

    ports::ProcessLaunch launch;
    launch.exe = layout.entry;
    launch.cwd = prefix.parent_path();
    switch (command.verb) {
        case ports::PrefixVerb::Boot: launch.args = {"wineboot", "-u"}; break;
        case ports::PrefixVerb::KillServer:
            launch.exe = layout.root / MacRuntimeLayout::kWineServer;
            launch.args = {"-k"};
            break;
        case ports::PrefixVerb::Run:
            launch.args = {command.exe.string()};
            launch.args.insert(launch.args.end(), command.args.begin(), command.args.end());
            launch.cwd = command.exe.parent_path();
            break;
    }
    launch.env = std::move(base);
    launch.stdio = ports::StdioMode::Capture;
    launch.own_group = true;
    return launch;
}

Result<std::optional<std::string>> DxmtWineRunnerPlatform::runtime_setup(const ports::RuntimeLayout&, CancelToken) {
    return std::nullopt;
}

std::optional<UserRequestKind> DxmtWineRunnerPlatform::pending_prerequisite() {
    if (cpu_ == HostCpu::AppleSilicon && !rosetta_installed()) return UserRequestKind::RosettaInstall;
    return std::nullopt;
}

}  // namespace rb::os_macos::runner
