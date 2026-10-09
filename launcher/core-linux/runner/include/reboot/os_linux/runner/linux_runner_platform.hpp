#pragma once

#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/os_linux/runner/slr_setup.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/ports/runner.hpp"

namespace reboot::os_linux::runner {

// Covers no capability ids (decisions linux-compat-layer, owner-2, process-model).
// Umu plays and Wine runs the CI smoke tests; hosting runs the native game server and never
// reaches this platform.
class LinuxRunnerPlatform final : public ports::IRunnerPlatform {
public:
    // `setup_base` is EnvBuilder's daemon-base layer; `umu_folders` is UMU_FOLDERS_PATH.
    LinuxRunnerPlatform(ports::IProcessLauncher& processes, ports::EnvBlock setup_base, NativePath umu_folders);

    // {Umu, Wine}. A missing python3 for umu-run is IPrerequisiteProbe's linux.python3, not a fallback.
    [[nodiscard]] std::vector<ports::RunnerKind> supported() const override;

    // Blocking (KronWineRunner::resolve, UmuInvocation::resolve with UMU_FOLDERS_PATH), so callers
    // run it on a worker. Umu without `dirs.launcher` fails with internal.bug.
    Result<ports::RuntimeLayout> layout(ports::RunnerKind kind, const ports::RuntimeDirs& dirs) override;

    // Nothing on Linux: the archives keep their file modes.
    Result<void> post_extract(const NativePath& runtime_dir) override;

    // `base` already holds the runner layer from EnvBuilder; this adds WINEPREFIX and, for an umu
    // layout, winhost's directory to PRESSURE_VESSEL_FILESYSTEMS_RW.
    Result<ports::ProcessLaunch> runner_launch(const ports::RuntimeLayout& layout, const NativePath& prefix,
                                               const NativePath& winhost_exe, ports::EnvBlock base) override;

    // Boot: `umu-run createprefix` for an umu layout, so umu and Proton build or upgrade the prefix;
    // `wine wineboot -u` for a Wine layout. KillServer runs the runtime's own `wineserver -k`. Run
    // exposes the exe's directory to pressure-vessel as runner_launch does winhost's.
    Result<ports::ProcessLaunch> prefix_command(const ports::RuntimeLayout& layout, const NativePath& prefix,
                                                const ports::PrefixCommand& command, ports::EnvBlock base) override;

    // Blocking SlrSetup::run for an umu layout, reporting "<runtime> <version>"; nullopt for a Wine layout.
    Result<std::optional<std::string>> runtime_setup(const ports::RuntimeLayout& layout, CancelToken token) override;

    // Always nullopt: no Linux prerequisite is answered through a user request.
    [[nodiscard]] std::optional<UserRequestKind> pending_prerequisite() override;

private:
    NativePath umu_folders_;
    SlrSetup slr_setup_;
};

}  // namespace reboot::os_linux::runner
