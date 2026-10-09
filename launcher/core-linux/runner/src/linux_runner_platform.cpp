#include "reboot/os_linux/runner/linux_runner_platform.hpp"

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "env_vars.hpp"
#include "messages.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/os_linux/runner/kron_wine_runner.hpp"
#include "reboot/os_linux/runner/umu_invocation.hpp"

namespace reboot::os_linux::runner {

namespace {

constexpr std::string_view kFilesystemsRw = "PRESSURE_VESSEL_FILESYSTEMS_RW";
// Relative to the GE-Proton root.
constexpr std::string_view kProtonWineServer = "files/bin/wineserver";

// Adds `dir` to what pressure-vessel shares read-write with the container.
Result<void> expose(ports::EnvBlock& env, const NativePath& dir) {
    const std::array<NativePath, 1> dirs{dir};
    auto exposed = UmuInvocation::filesystems_rw(value_of(env, kFilesystemsRw), dirs);
    if (!exposed) return std::unexpected(std::move(exposed.error()));
    set_var(env, kFilesystemsRw, std::move(*exposed));
    return {};
}

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
    if (UmuInvocation::is_umu_layout(layout))
        if (auto exposed = expose(base, winhost_exe.parent_path()); !exposed) return std::unexpected(exposed.error());

    ports::ProcessLaunch launch;
    launch.exe = layout.entry;
    launch.args = {winhost_exe.string()};
    launch.env = std::move(base);
    launch.cwd = winhost_exe.parent_path();
    launch.stdio = ports::StdioMode::Capture;
    launch.own_group = true;
    return launch;
}

Result<ports::ProcessLaunch> LinuxRunnerPlatform::prefix_command(const ports::RuntimeLayout& layout,
                                                                 const NativePath& prefix,
                                                                 const ports::PrefixCommand& command,
                                                                 ports::EnvBlock base) {
    const bool umu = UmuInvocation::is_umu_layout(layout);
    set_var(base, "WINEPREFIX", prefix.string());

    ports::ProcessLaunch launch;
    launch.exe = layout.entry;
    launch.cwd = prefix.parent_path();
    switch (command.verb) {
        case ports::PrefixVerb::Boot:
            launch.args = umu ? std::vector<std::string>{"createprefix"} : std::vector<std::string>{"wineboot", "-u"};
            break;
        case ports::PrefixVerb::KillServer:
            // -k signals the server named in the prefix's lock file, which the container shares with the host.
            launch.exe = layout.root / (umu ? kProtonWineServer : KronWineRunner::kWineServer);
            launch.args = {"-k"};
            break;
        case ports::PrefixVerb::Run:
            if (umu)
                if (auto exposed = expose(base, command.exe.parent_path()); !exposed)
                    return std::unexpected(exposed.error());
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

Result<std::optional<std::string>> LinuxRunnerPlatform::runtime_setup(const ports::RuntimeLayout& layout,
                                                                      CancelToken token) {
    if (!UmuInvocation::is_umu_layout(layout)) return std::nullopt;
    const auto build = slr_setup_.run(layout, std::move(token));
    if (!build) return std::unexpected(build.error());
    REBOOT_LOG_INFO(Play, "Steam Linux Runtime {} {} is set up", build->runtime, build->version);
    return build->runtime + " " + build->version;
}

std::optional<UserRequestKind> LinuxRunnerPlatform::pending_prerequisite() { return std::nullopt; }

}  // namespace reboot::os_linux::runner
