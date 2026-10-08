#pragma once

#include <utility>

#include "reboot/foundation/types.hpp"
#include "reboot/ports/ipc.hpp"

namespace reboot::os_linux::ipc {

// Covers no capability ids; ICallerContextProbe for the process that loaded reboot_client.
class LinuxCallerContext final : public ports::ICallerContextProbe {
public:
    // Reads the process once and cannot fail; anything unreadable counts as absent.
    // - display_env: every set, non-empty variable on linux-compat-layer's client allow-list,
    //   in environment order. The engine's play check needs DISPLAY or WAYLAND_DISPLAY among them.
    // - os_session: the audit session from /proc/self/sessionid, empty when unset (4294967295).
    // - interactive: /proc/self/loginuid holds a uid, i.e. the process descends from a desktop,
    //   TTY or SSH login; system services and containers have none.
    // - elevated: geteuid() is 0 while the login uid (or, without one, getuid()) is not, which
    //   catches sudo and setuid; a root login or a container's root is not elevated.
    [[nodiscard]] static LinuxCallerContext detect();

    [[nodiscard]] ports::CallerContext capture() const override { return context_; }
    // Nothing: X11 and Wayland have no foreground grant to hand over.
    void allow_foreground(u32 pid) override;

private:
    explicit LinuxCallerContext(ports::CallerContext context) : context_(std::move(context)) {}

    ports::CallerContext context_;
};

}  // namespace reboot::os_linux::ipc
