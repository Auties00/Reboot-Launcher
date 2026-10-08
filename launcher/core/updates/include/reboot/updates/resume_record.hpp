#pragma once

#include <optional>
#include <string>
#include <vector>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/updates/activity_probe.hpp"

namespace reboot::storage {
struct ResumeDocument;
}

namespace reboot::updates {

// state/resume.json: what the engine restores after `run --resume`. Taken once at the next start.
struct ResumeRecord {
    contracts::ipc::EngineOrigin origin{};
    // GUIs only: the CLI and test clients cannot be reopened.
    std::vector<contracts::ipc::ClientKind> reopen_clients;
    std::vector<HostProfileId> relaunch_hosts;
    // The newest payload a drained session pinned; ComponentStore keeps it as N-1.
    std::optional<SemVer> payload_version;
    std::vector<std::string> runtime_ids;

    bool operator==(const ResumeRecord&) const = default;
};

// Clients come from `at_open`; hosts and pins from `drained`, the snapshot of a consented drain.
[[nodiscard]] ResumeRecord make_resume_record(contracts::ipc::EngineOrigin origin, const ActivitySnapshot& at_open,
                                              const std::optional<ActivitySnapshot>& drained);

[[nodiscard]] ResumeRecord resume_record_from(const storage::ResumeDocument& document);
void store_resume_record(const ResumeRecord& record, storage::ResumeDocument& document);

// `run --resume`, keeping the origin.
[[nodiscard]] std::vector<std::string> resume_args(contracts::ipc::EngineOrigin origin);

}  // namespace reboot::updates
