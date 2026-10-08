#include "reboot/os_linux/runner/linux_runner_platform.hpp"

#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "env_vars.hpp"
#include "messages.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/os_linux/runner/kron_wine_runner.hpp"
#include "reboot/os_linux/runner/umu_invocation.hpp"

namespace reboot::os_linux::runner {

namespace {

constexpr std::string_view kFilesystemsRw = "PRESSURE_VESSEL_FILESYSTEMS_RW";

}  // namespace

LinuxRunnerPlatform::LinuxRunnerPlatform(ports::IProcessLauncher& processes, ports::EnvBlock setup_base,
                                         NativePath umu_folders)
    : umu_folders_(std::move(umu_folders)), slr_setup_(processes, std::move(setup_base), umu_folders_) {}

std::vector<ports::RunnerKind> LinuxRunnerPlatform::supported() const {
    return {ports::RunnerKind::Umu, ports::RunnerKind::Wine};
}

Result<ports::RuntimeLayout> LinuxRunnerPlatform::layout(ports::RunnerKind kind, const ports::RuntimeDirs& dirs) {
    switch (kind) {
        case ports::RunnerKind::Wine: {
            auto resolved = KronWineRunner::resolve(dirs.runtime);
            if (!resolved) return std::unexpected(std::move(resolved.error()));
            return resolved->to_runtime_layout();
        }
        case ports::RunnerKind::Umu: {
            if (!dirs.launcher) return std::unexpected(internal_bug("LinuxRunnerPlatform::layout(Umu): no launcher"));
            auto resolved = UmuInvocation::resolve(*dirs.launcher, dirs.runtime, umu_folders_);
            if (!resolved) return std::unexpected(std::move(resolved.error()));
            return resolved->to_runtime_layout();
        }
        case ports::RunnerKind::Native:
        case ports::RunnerKind::MacRuntime: break;
    }
    return make_diag(ErrorDomain::Platform, kRunnerKindUnsupported)
        .arg("runner", kind)
        .kind(ErrorKind::Unsupported)
        .fail();
}

Result<void> LinuxRunnerPlatform::post_extract(const NativePath&) { return {}; }

Result<ports::ProcessLaunch> LinuxRunnerPlatform::runner_launch(const ports::RuntimeLayout& layout,
                                                                const NativePath& prefix,
                                                                const NativePath& winhost_exe, ports::EnvBlock base) {
    // string() is the native bytes on POSIX.
    set_var(base, "WINEPREFIX", prefix.string());
    if (UmuInvocation::is_umu_layout(layout)) {
        const std::array<NativePath, 1> winhost_dir{winhost_exe.parent_path()};
        auto exposed = UmuInvocation::filesystems_rw(value_of(base, kFilesystemsRw), winhost_dir);
        if (!exposed) return std::unexpected(std::move(exposed.error()));
        set_var(base, kFilesystemsRw, std::move(*exposed));
    }

    ports::ProcessLaunch launch;
    launch.exe = layout.entry;
    launch.args = {winhost_exe.string()};
    launch.env = std::move(base);
    launch.cwd = winhost_exe.parent_path();
    launch.stdio = ports::StdioMode::Capture;
    launch.own_group = true;
    return launch;
}

Result<void> LinuxRunnerPlatform::runtime_setup(const ports::RuntimeLayout& layout, CancelToken token) {
    if (!UmuInvocation::is_umu_layout(layout)) return {};
    const auto build = slr_setup_.run(layout, std::move(token));
    if (!build) return std::unexpected(build.error());
    REBOOT_LOG_INFO(Play, "Steam Linux Runtime {} {} is set up", build->runtime, build->version);
    return {};
}

std::optional<UserRequestKind> LinuxRunnerPlatform::pending_prerequisite() { return std::nullopt; }

}  // namespace reboot::os_linux::runner
