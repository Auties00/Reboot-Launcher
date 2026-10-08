#include "reboot/os_macos/runner/dxmt_wine_runner_platform.hpp"

#include <sys/xattr.h>

#include <cerrno>
#include <filesystem>
#include <system_error>
#include <utility>

#include "messages.hpp"
#include "reboot/os_macos/runner/mac_runtime_layout.hpp"
#include "reboot/posix/posix_error.hpp"

namespace reboot::os_macos::runner {

namespace {

namespace fs = std::filesystem;

constexpr const char* kQuarantineAttribute = "com.apple.quarantine";
// Rosetta 2's translation daemon; it exists only once Rosetta is installed.
constexpr std::string_view kRosettaDaemon = "/Library/Apple/usr/libexec/oahd";

// ENOATTR: never set. ENOTSUP: the volume has no xattrs.
Result<void> strip_quarantine(const NativePath& path) {
    if (::removexattr(path.c_str(), kQuarantineAttribute, XATTR_NOFOLLOW) == 0) return {};
    const int error = errno;
    if (error == ENOATTR || error == ENOTSUP) return {};
    return make_diag(ErrorDomain::Platform, kQuarantineStripFailed).arg("path", path).os(posix::errno_error(error)).fail();
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
    std::erase_if(base.vars, [](const auto& var) { return var.first == "WINEPREFIX"; });
    // string() is the native bytes on POSIX.
    base.vars.emplace_back("WINEPREFIX", prefix.string());

    ports::ProcessLaunch launch;
    launch.exe = layout.entry;
    launch.args = {winhost_exe.string()};
    launch.env = std::move(base);
    launch.cwd = winhost_exe.parent_path();
    launch.stdio = ports::StdioMode::Capture;
    launch.own_group = true;
    return launch;
}

Result<void> DxmtWineRunnerPlatform::runtime_setup(const ports::RuntimeLayout&, CancelToken) { return {}; }

std::optional<UserRequestKind> DxmtWineRunnerPlatform::pending_prerequisite() {
    if (cpu_ == HostCpu::AppleSilicon && !rosetta_installed()) return UserRequestKind::RosettaInstall;
    return std::nullopt;
}

}  // namespace reboot::os_macos::runner
