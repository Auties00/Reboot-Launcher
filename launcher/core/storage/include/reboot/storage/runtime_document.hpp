#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/json/object.hpp>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/storage/enum_names.hpp"
#include "reboot/storage/load_report.hpp"

namespace reboot::storage {

enum class ChildRole : u8 { Backend, GameServer, Winhost, Game, Companion };
enum class EnginePortRole : u8 { Front, GameChannel, LegacyFixed };

template <>
struct EnumNames<ChildRole> {
    static constexpr std::array<std::string_view, 5> kNames{"backend", "game_server", "winhost", "game", "companion"};
};
template <>
struct EnumNames<EnginePortRole> {
    static constexpr std::array<std::string_view, 3> kNames{"front", "game_channel", "legacy_fixed"};
};

// A pid is only reaped when its creation time still matches, so a reused pid is left alone.
struct RecordedProcess {
    u32 pid = 0;
    std::chrono::system_clock::time_point created;
    ChildRole role{};
    std::optional<SessionId> session;
    // Its listeners (the backend port, a host's port block), so a busy port can be traced to an orphan.
    std::vector<Port> ports;

    bool operator==(const RecordedProcess&) const = default;
};

struct EnginePort {
    Port port;
    EnginePortRole role{};

    bool operator==(const EnginePort&) const = default;
};

// state/runtime.json: what the engine runs, so the next start can reap a crashed engine's orphans.
struct RuntimeDocument {
    static constexpr std::string_view kName = "runtime";
    static constexpr u32 kSchema = 1;

    u32 engine_pid = 0;
    std::chrono::system_clock::time_point engine_created;
    std::string engine_build;
    contracts::ipc::EngineOrigin origin{};
    // Pipe name or socket path, for diagnostics; clients derive the endpoint themselves.
    NativePath endpoint;
    std::vector<EnginePort> engine_ports;
    std::vector<RecordedProcess> children;
    boost::json::object unknown;

    [[nodiscard]] static RuntimeDocument read(const boost::json::object& values, std::vector<ValueIssue>& issues);
    [[nodiscard]] boost::json::object write() const;
    [[nodiscard]] static Result<boost::json::object> upgrade(boost::json::object values, u32 from_schema);
};

}  // namespace reboot::storage
