#include "reboot/os_linux/platform/tarball_layout.hpp"

#include "messages.hpp"
#include "reboot/os_linux/platform/xdg_paths.hpp"

namespace rb::os_linux::platform {

Result<EngineCommand> stable_engine_command(const XdgPaths& paths) {
    switch (paths.install_kind()) {
        case ports::InstallKind::Tarball: return EngineCommand{.exe = TarballLayout{*paths.tarball_root()}.shim()};
        case ports::InstallKind::AppImage: return EngineCommand{.exe = *paths.appimage(), .leading_args = {"engine"}};
        default: break;
    }
    return make_diag(ErrorDomain::Platform, kIntegrationNeedsUserInstall)
        .arg("entry", "reboot-engine")
        .kind(ErrorKind::Unsupported)
        .fail();
}

}  // namespace rb::os_linux::platform
