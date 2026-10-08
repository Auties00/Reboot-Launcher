#pragma once

#include <string_view>

namespace reboot::front {

// Inbound headers with this prefix are dropped, so only the front sets them on requests to our backend.
inline constexpr std::string_view kRebootHeaderPrefix = "X-Reboot-";

// The session key in hex; origin and console key come from that session's ConfigureSession.
inline constexpr std::string_view kSessionHeader = "X-Reboot-Session";

}  // namespace reboot::front
