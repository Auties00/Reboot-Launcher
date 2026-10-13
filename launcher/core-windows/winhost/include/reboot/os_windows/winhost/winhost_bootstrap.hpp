#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/os_windows/winhost/winhost_failure.hpp"

namespace rb::os_windows::winhost {

inline constexpr std::size_t kControlTokenSize = 32;

using ControlTokenBytes = Secret<std::array<u8, kControlTokenSize>>;

// Covers no capability ids (decisions process-model, game-control-channel).
// What winhost needs to reach the engine's game channel: REBOOT_CTL from its environment, and its
// control token as one "<token>\n" line on stdin. Wine forks the game from winhost's Unix
// environment, so a token there would show in the game's /proc/<pid>/environ.
struct WinhostBootstrap {
    u16 port = 0;
    ControlTokenBytes token;
};

// Accepts only tcp://127.0.0.1:<port> with a port in 1..65535, so winhost never dials off-host.
[[nodiscard]] std::optional<u16> parse_ctl_endpoint(std::string_view value) noexcept;

// The token text: unpadded base64url of exactly kControlTokenSize bytes (43 chars).
[[nodiscard]] std::optional<ControlTokenBytes> decode_control_token(std::string_view value);

// Takes REBOOT_CTL out of the environment and reads the token line from stdin, reading nothing past
// it, and wipes the copies it read. FailureStep::Bootstrap when either is missing or malformed.
[[nodiscard]] Expected<WinhostBootstrap> read_bootstrap();

}  // namespace rb::os_windows::winhost
