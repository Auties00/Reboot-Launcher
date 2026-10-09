#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "reboot/contracts/game_client.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::game_channel {

// Malformed and out-of-order frames use contracts.malformed_frame and contracts.unexpected_frame.
enum class GameChannelErrorCode : u8 {
    ListenFailed,
    NotListening,
    BadPreamble,
    PayloadAbiMismatch,
    ProtocolMismatch,
    HelloTimeout,
    UnknownToken,
    RoleMismatch,
    DuplicatePeer,
    PeerLost,
    NotWelcomed,
    UnsupportedRequest,
    RequestFailed,
    TestModeOff,
    LogUnreadable,
};

// Package-internal; every public function returns it as a Diagnostic through to_diagnostic.
struct GameChannelError {
    GameChannelErrorCode code = GameChannelErrorCode::PeerLost;
    // The token's role; RoleMismatch's expected role.
    std::optional<contracts::game_client::PeerRole> role;
    // RoleMismatch: the role the Hello claimed.
    std::optional<contracts::game_client::PeerRole> presented_role;
    std::string module;
    std::string request;
    // PayloadAbiMismatch and ProtocolMismatch.
    std::optional<u32> expected_version;
    std::optional<u32> actual_version;
    std::optional<std::chrono::milliseconds> timeout;
    std::optional<NativePath> path;
    std::optional<SystemError> os_error;
    // RequestFailed: the peer's own diagnostic.
    std::optional<Diagnostic> cause;
};

[[nodiscard]] Diagnostic to_diagnostic(const GameChannelError& error);

}  // namespace rb::game_channel
