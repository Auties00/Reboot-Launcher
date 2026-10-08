#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/os_services.hpp"

namespace reboot::os_linux::platform {

class XdgPaths;

// Covers no capability ids; IIntegrationRegistrar over per-user .desktop files, xdg-mime and
// systemd user units, for AppImage and tarball installs only (else
// platform.integration_needs_user_install). Commands name stable_engine_command or $APPIMAGE,
// never versions/<v> or the AppImage mount. Helpers (xdg-mime, systemctl) are bounded by 5 s each.
class LinuxIntegrationRegistrar final : public ports::IIntegrationRegistrar {
public:
    // `paths` outlives the registrar. `root_hash16` names the socket; `data_root_override` is
    // set as REBOOT_LAUNCHER_HOME in the autostart entry.
    LinuxIntegrationRegistrar(const XdgPaths& paths, std::string root_hash16,
                              std::optional<NativePath> data_root_override);

    // Ours when an entry runs a stable entry of this install, Stale when that file is gone,
    // Foreign otherwise; `detail` holds the command line. Autostart is also Stale, with detail
    // "bypasses-engine-agent", when it runs the engine directly while the units are installed.
    // EngineAgent without a systemd user manager is Absent with detail "no-systemd".
    Result<ports::IntegrationStatus> status(ports::IntegrationKind kind) override;
    // UrlScheme: reboot-launcher-url.desktop running "<exe>" --activate-url %u, then `xdg-mime
    // default`; a Foreign handler is kept (platform.integration_foreign). DesktopEntry:
    // reboot-launcher.desktop.
    // Autostart: `systemctl --user start reboot-engine.service` while the units are installed,
    // since a directly run engine would unlink the socket systemd listens on; otherwise
    // "<engine>" run --origin=service-manager. A user opt-out is left as set.
    // EngineAgent (default data root only, else platform.engine_agent_default_root_only):
    // reboot-engine.socket (ListenStream=%t/reboot-launcher/<hash16>.sock, SocketMode=0600,
    // DirectoryMode=0700) and a Restart=on-failure service with StartLimitBurst=3, so an update
    // that crashes before its self-test stops restarting. The service pins XDG_DATA_HOME,
    // XDG_CACHE_HOME and XDG_STATE_HOME, which the user manager often lacks. Rewrites an
    // existing Autostart entry to match.
    Result<void> apply(ports::IntegrationKind kind, const NativePath& exe) override;
    // Only what status reports as Ours or Stale. EngineAgent stops and disables the socket first
    // and points an existing Autostart entry back at the engine.
    Result<void> remove(ports::IntegrationKind kind) override;

private:
    const XdgPaths& paths_;
    std::string root_hash16_;
    std::optional<NativePath> data_root_override_;
};

}  // namespace reboot::os_linux::platform
