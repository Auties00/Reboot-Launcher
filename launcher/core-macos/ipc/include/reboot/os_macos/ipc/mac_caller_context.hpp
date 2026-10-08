#pragma once

#include <utility>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/ipc.hpp"

namespace reboot::os_macos::ipc {

// Covers no capability ids; ICallerContextProbe for the process that loaded reboot_client.
// make_client_platform detects it once and hands the same context to SmAppServiceEngineStarter.
class MacCallerContext final : public ports::ICallerContextProbe {
public:
    // Reads the process once with SessionGetInfo(callerSecuritySession). os_session: the audit
    // session id in decimal, which the engine compares with its own for play. interactive: the
    // session has sessionHasGraphicAccess, i.e. it is the user's Aqua session, which rules out
    // SSH logins. elevated: geteuid() == 0. display_env stays empty. A failed read is
    // platform.caller_session_unreadable.
    [[nodiscard]] static Result<MacCallerContext> detect();

    [[nodiscard]] ports::CallerContext capture() const override { return context_; }
    // Nothing: macOS has no foreground grant to hand over.
    void allow_foreground(u32 pid) override;

private:
    explicit MacCallerContext(ports::CallerContext context) : context_(std::move(context)) {}

    ports::CallerContext context_;
};

}  // namespace reboot::os_macos::ipc
