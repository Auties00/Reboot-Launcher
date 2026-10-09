#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "reboot/contracts/game_server.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/gameserver/game_server_description.hpp"

namespace rb::gameserver {

// IP-first: `address` is an IP or CIDR block; an account-id ban alone is evadable, since ids are
// self-asserted.
using Ban = contracts::game_server::Ban;

struct GameTarget {
    GameVersion version;
    Changelist cl;
    std::optional<NativePath> build_root;
};

// One port per declared socket, in declaration order. The server binds exactly these, with no
// next-port fallback, and keeps them across Reset.
struct ListenSpec {
    IpAddress bind_address = IpAddress::v4(0);
    std::vector<Port> ports;
};

struct BackendAccess {
    std::string origin;
    std::string account_id;
    SecretString service_token;
};

struct AutoAtPlayers {
    u32 players = 0;
};
struct ManualStart {};
using MatchStartPolicy = std::variant<AutoAtPlayers, ManualStart>;

struct MatchSettings {
    std::string playlist;
    MatchStartPolicy start = ManualStart{};
    u32 max_players = 0;
    u32 tick_rate = 0;
};

// What the host session asks of one game-server process; the session id and log folder are
// added by GameServerProcess. Move-only because of the backend token.
struct GameServerConfig {
    GameTarget game;
    ListenSpec listen;
    std::optional<BackendAccess> backend;
    MatchSettings match;
    // Remote operators must come from one of these; the engine channel is always trusted.
    std::vector<std::string> operator_cidrs;
    std::vector<Ban> bans;
};

// gameserver.port_count_mismatch, invalid_port, bind_not_ipv4, backend_required (when the
// description needs one), invalid_match_setting, invalid_address, path_not_absolute (build_root).
[[nodiscard]] Result<void> validate(const GameServerConfig& config, const GameServerDescription& description);

// The ServerWelcome payload. It copies the backend token into plain strings, so the caller
// encodes it at once and drops it.
[[nodiscard]] contracts::game_server::ServerConfig to_wire(const GameServerConfig& config, const SessionId& session,
                                                           const NativePath& log_dir);

}  // namespace rb::gameserver
