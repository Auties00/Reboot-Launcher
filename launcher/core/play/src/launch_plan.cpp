#include "reboot/play/launch_plan.hpp"

namespace reboot::play {

ports::SessionLaunch LaunchPlan::session_launch(SessionId session) const {
    ports::SessionLaunch launch;
    launch.session = session;
    launch.exe = exe;
    launch.args = args.argv();
    launch.env = env.copy();
    launch.cwd = cwd;
    launch.companions = companions;
    launch.inject = inject;
    launch.park = park;
    launch.multiplier = multiplier;
    return launch;
}

std::vector<ports::CompanionSpec> companions_for(const builds::BuildLayout& layout) {
    std::vector<ports::CompanionSpec> out;
    if (layout.launcher_exe) out.push_back(ports::CompanionSpec{layout.root / *layout.launcher_exe, {}});
    if (layout.eac_exe) out.push_back(ports::CompanionSpec{layout.root / *layout.eac_exe, {}});
    return out;
}

std::vector<NativePath> parked_for(const builds::BuildLayout& layout) {
    std::vector<NativePath> out;
    out.reserve(layout.aftermath_dlls.size());
    for (const NativePath& dll : layout.aftermath_dlls) out.push_back(layout.root / dll);
    return out;
}

}  // namespace reboot::play
