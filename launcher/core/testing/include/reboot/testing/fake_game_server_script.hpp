#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "reboot/contracts/common.hpp"
#include "reboot/contracts/game_server.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/testing/child_misbehaviour.hpp"

namespace rb::testing {

using ServerScriptEvent =
    std::variant<contracts::game_server::StateChanged, contracts::game_server::PlayerJoined,
                 contracts::game_server::PlayerLeft, contracts::game_server::PlayerCount,
                 contracts::game_server::MatchEnded, contracts::game_server::Fatal, contracts::common::Log>;

// Sent `at` after Listening.
struct TimedServerEvent {
    std::chrono::milliseconds at{0};
    ServerScriptEvent event;
};

// Build "fake", every version, one Game socket, in-process reset, no backend, no operator commands.
[[nodiscard]] contracts::game_server::GameServerDescription default_fake_description();

// What FakeGameServer does. The defaults describe a conforming reboot-game-server.
struct FakeGameServerScript {
    ChildMisbehaviour child;
    contracts::game_server::GameServerDescription description = default_fake_description();
    // ServerHello carries this instead, for the describe-cache mismatch.
    std::optional<contracts::game_server::GameServerDescription> hello_description;
    // --describe writes nothing and exits with this code.
    std::optional<int> describe_exit_code;
    // Between Welcome and Listening.
    std::chrono::milliseconds listen_delay{0};
    // Binds every Welcome port for real (UDP, no SO_REUSEADDR) and answers the rbsb probe on the
    // game port; an occupied port gives ListenFailed and kBindFailureExitCode. Off, Listening is
    // reported without any socket.
    bool bind_sockets = true;
    // Reports ListenFailed for the socket at this index whatever the port's state.
    std::optional<std::size_t> bind_failure_index;
    std::vector<TimedServerEvent> events;
    // StartMatch moves to InProgress and MatchEnded{Completed} follows after this long; without it
    // the match runs until EndMatch.
    std::optional<std::chrono::milliseconds> match_length;
};

// The JSON form follows parse_fake_backend_script's rules.
[[nodiscard]] Result<FakeGameServerScript> parse_fake_game_server_script(std::string_view json);
[[nodiscard]] Result<FakeGameServerScript> load_fake_game_server_script(const NativePath& file);
[[nodiscard]] std::string to_json(const FakeGameServerScript& script);

}  // namespace rb::testing
