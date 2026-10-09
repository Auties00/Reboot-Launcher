#pragma once

#include <expected>
#include <optional>
#include <string_view>

#include "reboot/contracts/common.hpp"
#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::os_windows::winhost {

// Where winhost failed; its name is the WhFatal step.
enum class FailureStep : u8 {
    Bootstrap,
    Connect,
    Handshake,
    Protocol,
    Spawn,
    Inject,
    Resume,
    Stop,
    Relay,
};

// winhost cannot link the compiled foundation (no message registry), so it reports typed
// failures; `os_code` is a Windows error code from inside the prefix.
struct WinhostFailure {
    FailureStep step{};
    std::optional<i64> os_code;
};

template <class T>
using Expected = std::expected<T, WinhostFailure>;

[[nodiscard]] std::string_view step_name(FailureStep step) noexcept;

[[nodiscard]] contracts::winhost::WhFatal to_fatal(const WinhostFailure& failure);

// The failed reply to an engine request: game_channel.request_failed{role, request}, with the
// os code as SystemError::Origin::GuestWindows. The id is registered by core/game_channel.
[[nodiscard]] contracts::common::CommandResult failed_reply(u64 req_id, std::string_view request,
                                                            const WinhostFailure& failure);

}  // namespace rb::os_windows::winhost
