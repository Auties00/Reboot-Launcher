#pragma once

#include <memory>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/os_services.hpp"

namespace rb::os_linux::platform {

// Covers no capability ids; IShellLauncher over the XDG desktop portal, xdg-open and GIO.
// The engine often runs as a systemd user service without DISPLAY or WAYLAND_DISPLAY, so the
// session-bus portal comes first: it opens things in the user's graphical session. D-Bus and
// trash calls go through libgio-2.0.so.0, loaded with dlopen; without it only xdg-open remains
// and trash fails. xdg-open runs with the engine's environment. Each call is bounded by 5 s.
class XdgShell final : public ports::IShellLauncher {
public:
    XdgShell();
    ~XdgShell() override;
    XdgShell(const XdgShell&) = delete;
    XdgShell& operator=(const XdgShell&) = delete;

    // org.freedesktop.portal.OpenURI.OpenURI, then xdg-open. Anything but an https URL fails
    // with platform.url_not_https; no opener at all fails with platform.no_opener.
    Result<void> open_url(std::string_view https_url) override;
    // OpenURI.OpenFile with an O_PATH fd (the portal refuses file:// URIs), then xdg-open.
    Result<void> open_path(const NativePath& path) override;
    // org.freedesktop.FileManager1.ShowItems, then OpenURI.OpenDirectory, then open_path on the parent.
    Result<void> reveal(const NativePath& path) override;
    // g_file_trash, as `gio trash` does; a volume with no trash fails with
    // platform.trash_unavailable rather than deleting permanently.
    Result<void> trash(const NativePath& path) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::os_linux::platform
