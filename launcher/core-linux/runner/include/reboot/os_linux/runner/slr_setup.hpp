#pragma once

#include <string_view>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/os_linux/runner/slr_build.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/ports/runner.hpp"

namespace reboot::os_linux::runner {

// Covers no capability ids (decisions linux-compat-layer, process-model).
// The explicit Steam Linux Runtime install or update for umu, never run inside a session.
class SlrSetup {
public:
    // Under UMU_FOLDERS_PATH; idle by construction, so it is recreated and removed on every run.
    static constexpr std::string_view kScratchPrefix = "setup-prefix";
    static constexpr std::string_view kScopeName = "reboot-runtime-setup";

    // `base` is EnvBuilder's daemon-base layer; `folders` is UMU_FOLDERS_PATH.
    SlrSetup(ports::IProcessLauncher& processes, ports::EnvBlock base, NativePath folders);

    // Blocking, so callers run it on a worker. Runs `<layout.entry> ""` with UMU_RUNTIME_UPDATE=1,
    // which sets up only the runtime and the scratch prefix; output goes to the Wine log.
    // Cancelling kills the process tree. Errors: platform.linux_scratch_prefix_failed,
    // platform.linux_slr_setup_not_started, platform.linux_slr_setup_failed,
    // platform.linux_slr_setup_killed, platform.linux_slr_setup_cancelled, or SlrBuild::read's.
    [[nodiscard]] Result<SlrBuild> run(const ports::RuntimeLayout& layout, CancelToken token);

private:
    ports::IProcessLauncher& processes_;
    ports::EnvBlock base_;
    NativePath folders_;
};

}  // namespace reboot::os_linux::runner
