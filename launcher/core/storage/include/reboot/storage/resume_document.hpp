#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/json/object.hpp>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/storage/load_report.hpp"

namespace reboot::storage {

// The update the engine is restarting into. After two failed attempts the previous version is restored.
struct PendingUpdate {
    SemVer from;
    SemVer to;
    u32 attempts = 0;

    bool operator==(const PendingUpdate&) const = default;
};

// state/resume.json: written by updates before a restart, applied by `run --resume`, then cleared.
struct ResumeDocument {
    static constexpr std::string_view kName = "resume";
    static constexpr u32 kSchema = 1;

    contracts::ipc::EngineOrigin origin{};
    std::vector<contracts::ipc::ClientKind> reopen_clients;
    std::vector<HostProfileId> relaunch_hosts;
    std::optional<SemVer> payload_version;
    std::vector<std::string> runtime_ids;
    std::optional<PendingUpdate> pending_update;
    boost::json::object unknown;

    [[nodiscard]] static ResumeDocument read(const boost::json::object& values, std::vector<ValueIssue>& issues);
    [[nodiscard]] boost::json::object write() const;
    [[nodiscard]] static Result<boost::json::object> upgrade(boost::json::object values, u32 from_schema);
};

}  // namespace reboot::storage
