#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "reboot/contracts/common.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

// reboot-game-server --control=stdio, and --describe, which writes one GameServerDescription
// frame to stdout and exits. Requests carry req_id and get a common::CommandResult or
// common::Unsupported.
namespace reboot::contracts::game_server {

inline constexpr u32 kGameServerProtocol = VersionStreams::game_server_protocol;

// The rbsb/1 reachability probe the game port must answer.
inline constexpr std::array<u8, 25> kRbsbProbe{0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                               0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x04};

enum class SocketRole : u8 { Game, Beacon };
enum class StartPolicy : u8 { AutoAtPlayers, Manual };
enum class ListenStage : u8 { Bind, Listen, Probe };
enum class MatchState : u8 { Lobby, Warmup, InProgress, Ending };
enum class MatchEndReason : u8 { Completed, Aborted, Operator };

struct ClRange {
    u32 first = 0;
    u32 last = 0;
};

// Versions are GameVersion::canonical() strings; an empty cl_ranges covers every changelist.
struct VersionSupport {
    std::string version_min;
    std::string version_max;
    std::vector<ClRange> cl_ranges;
};

struct SocketSpec {
    SocketRole role{};
};

struct ServerCapabilities {
    bool in_process_reset = false;
    bool needs_backend = false;
    std::vector<std::string> operator_commands;
};

// Cached by the binary's sha256; it sizes the port block before spawn.
struct GameServerDescription {
    u32 protocol = 0;
    std::string build;
    std::vector<VersionSupport> supports;
    std::vector<SocketSpec> sockets;
    ServerCapabilities capabilities;
};
REBOOT_CONTRACT_FRAME(GameServerDescription, 0x300)

// Must equal the cached description.
struct ServerHello {
    GameServerDescription description;
};
REBOOT_CONTRACT_FRAME(ServerHello, 0x301)

struct GameSpec {
    std::string version;
    u32 cl = 0;
    std::optional<WirePath> build_root;
};

// One port per declared socket, in declaration order; bound exactly, never a next-port fallback.
struct ListenConfig {
    std::string bind_address;
    std::vector<u16> ports;
};

struct BackendLink {
    std::string origin;
    std::string account_id;
    std::string service_token;
};

// `start_at_players` applies to AutoAtPlayers only.
struct MatchConfig {
    std::string playlist;
    StartPolicy start_policy{};
    u32 start_at_players = 0;
    u32 max_players = 0;
    u32 tick_rate = 0;
};

struct OperatorConfig {
    std::vector<std::string> ip_cidrs;
};

// IP-first: `address` is an IP or CIDR; the account id only narrows it.
struct Ban {
    std::string address;
    std::optional<std::string> account_id;
    std::optional<u64> expires_unix_ms;
    std::string reason;
};

struct ServerConfig {
    Uuid session_id;
    GameSpec game;
    ListenConfig listen;
    std::optional<BackendLink> backend;
    MatchConfig match;
    OperatorConfig operators;
    std::vector<Ban> bans;
    WirePath log_dir;
};

struct ServerWelcome {
    ServerConfig config;
};
REBOOT_CONTRACT_FRAME(ServerWelcome, 0x302)

// server -> engine events

struct BoundSocket {
    SocketRole role{};
    u16 port = 0;
};

struct Listening {
    std::vector<BoundSocket> bound;
};
REBOOT_CONTRACT_FRAME(Listening, 0x310)

struct ListenFailed {
    u16 port = 0;
    i64 os_error = 0;
    ListenStage stage{};
};
REBOOT_CONTRACT_FRAME(ListenFailed, 0x311)

struct StateChanged {
    MatchState state{};
};
REBOOT_CONTRACT_FRAME(StateChanged, 0x312)

// `account_id` is what the client claimed; the server has not verified it.
struct PlayerJoined {
    u32 player_id = 0;
    std::string account_id;
    std::string display_name;
    std::string address;
};
REBOOT_CONTRACT_FRAME(PlayerJoined, 0x313)

struct PlayerLeft {
    u32 player_id = 0;
    std::string account_id;
};
REBOOT_CONTRACT_FRAME(PlayerLeft, 0x314)

struct PlayerCount {
    u32 n = 0;
};
REBOOT_CONTRACT_FRAME(PlayerCount, 0x315)

struct Placement {
    std::string account_id;
    u32 place = 0;
};

struct MatchEnded {
    MatchEndReason reason{};
    std::optional<std::string> winner;
    std::vector<Placement> placements;
};
REBOOT_CONTRACT_FRAME(MatchEnded, 0x316)

struct Fatal {
    std::string code;
    std::string detail;
};
REBOOT_CONTRACT_FRAME(Fatal, 0x317)

// engine -> server requests

struct StartMatch {
    u64 req_id = 0;
    u32 countdown_s = 0;
};
REBOOT_CONTRACT_FRAME(StartMatch, 0x320)

struct EndMatch {
    u64 req_id = 0;
};
REBOOT_CONTRACT_FRAME(EndMatch, 0x321)

// Keeps the bound port block.
struct Reset {
    u64 req_id = 0;
};
REBOOT_CONTRACT_FRAME(Reset, 0x322)

struct Kick {
    u64 req_id = 0;
    u32 player_id = 0;
    std::string reason;
};
REBOOT_CONTRACT_FRAME(Kick, 0x323)

struct SetBans {
    u64 req_id = 0;
    std::vector<Ban> bans;
};
REBOOT_CONTRACT_FRAME(SetBans, 0x324)

struct SetOperators {
    u64 req_id = 0;
    std::vector<std::string> ip_cidrs;
};
REBOOT_CONTRACT_FRAME(SetOperators, 0x325)

struct RunCommand {
    u64 req_id = 0;
    std::string text;
};
REBOOT_CONTRACT_FRAME(RunCommand, 0x326)

struct Drain {
    u64 req_id = 0;
};
REBOOT_CONTRACT_FRAME(Drain, 0x327)

struct Shutdown {
    u64 req_id = 0;
    u32 grace_ms = 0;
};
REBOOT_CONTRACT_FRAME(Shutdown, 0x328)

}  // namespace reboot::contracts::game_server
