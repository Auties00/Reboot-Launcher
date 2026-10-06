#pragma once

#include <string_view>

namespace sb::ops {

// systemd readiness protocol (Type=notify); a no-op when NOTIFY_SOCKET is unset.
void sd_notify(std::string_view state) noexcept;

}  // namespace sb::ops
