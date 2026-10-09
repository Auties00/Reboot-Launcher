#pragma once

#include <utility>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/ipc.hpp"

namespace rb::os_windows::ipc {

// Covers no capability ids; ICallerContextProbe for the process that loaded reboot_client.
class WindowsCallerContext final : public ports::ICallerContextProbe {
public:
    // Interactive needs a visible window station outside session 0, which rules out services,
    // S4U tasks and SSH logons. A failing call is platform.ipc_call_failed.
    [[nodiscard]] static Result<WindowsCallerContext> detect();

    [[nodiscard]] ports::CallerContext capture() const override { return context_; }
    // Fails unless this process may set the foreground itself; that failure is expected and ignored.
    void allow_foreground(u32 pid) override;

private:
    explicit WindowsCallerContext(ports::CallerContext context) : context_(std::move(context)) {}

    ports::CallerContext context_;
};

}  // namespace rb::os_windows::ipc
