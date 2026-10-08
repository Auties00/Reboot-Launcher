#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::gameserver {

enum class GameServerErrorCode : u8 {
    PathNotAbsolute,
    ExeUnreadable,
    DescribeSpawnFailed,
    DescribeTimeout,
    // The process exited without writing a description frame.
    DescribeNoOutput,
    DescribeMalformed,
    ProtocolMismatch,
    // A description must declare exactly one Game socket.
    InvalidSockets,
    // A ServerHello that differs from the cached description of the same binary.
    DescriptionMismatch,
    PortCountMismatch,
    // Zero, or the same port twice in one block.
    InvalidPort,
    BindNotIpv4,
    BackendRequired,
    InvalidMatchSetting,
    // An operator allowlist or ban entry that is not an IP address or CIDR block.
    InvalidAddress,
    SessionDirFailed,
    AlreadyStarted,
    NotRunning,
    StoppedBeforeStart,
    CommandNotDeclared,
    // No reply to an operator command within its deadline; the reply is ignored if it comes later.
    CommandTimeout,
};

struct GameServerError {
    GameServerErrorCode code = GameServerErrorCode::NotRunning;
    std::optional<NativePath> path;
    std::optional<u64> expected;
    std::optional<u64> actual;
    std::optional<Port> port;
    std::optional<std::chrono::milliseconds> timeout;
    // InvalidMatchSetting: the member name, e.g. "max_players".
    std::string field;
    std::string address;
    std::string command;
    std::optional<Diagnostic> cause;
};

[[nodiscard]] Diagnostic to_diagnostic(const GameServerError& error);

}  // namespace reboot::gameserver
