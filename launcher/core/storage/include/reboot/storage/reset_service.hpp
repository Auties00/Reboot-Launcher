#pragma once

#include <string>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/storage/key.hpp"

namespace reboot::storage {

class Settings;
class SettingsRegistry;

// What still runs on the settings a reset would change.
struct ResetBlockers {
    std::vector<SessionId> sessions;
    bool backend_running = false;

    [[nodiscard]] bool empty() const noexcept { return sessions.empty() && !backend_running; }
};

struct ResetReport {
    u64 revision = 0;
    std::vector<std::string> keys;
};

// Supplied by the engine, which owns sessions, the backend lease and host profiles. Strand-only.
struct ResetHooks {
    UniqueFunction<ResetBlockers(ResetGroup)> find_blockers;
    // Stops what find_blockers returned; `done` runs on the strand.
    UniqueFunction<void(ResetGroup, ResetBlockers, CancelToken, UniqueFunction<void(Result<void>)>)> stop_blockers;
    // Resets what a group owns outside settings.json: for Host, every profile's fields and listing.
    UniqueFunction<Result<void>(ResetGroup)> reset_records;
};

// Capabilities: settings-storage.per-tab-reset.
// Strand-only. Resets a tab's records, then its keys as one revision; builds and identities stay.
class ResetService {
public:
    ResetService(Settings& settings, const SettingsRegistry& registry, OpRegistry& ops, ResetHooks hooks);
    ResetService(const ResetService&) = delete;
    ResetService& operator=(const ResetService&) = delete;

    // Fails with storage.reset_blocked while anything find_blockers reports still runs.
    Result<ResetReport> reset(ResetGroup group);
    // Completes with the ResetReport, or fails with storage.reset_stop_failed if stopping fails.
    Result<OpHandle> start_reset_after_stop(ResetGroup group, DisconnectPolicy policy);

private:
    Result<ResetReport> reset_unblocked(ResetGroup group);

    Settings& settings_;
    const SettingsRegistry& registry_;
    OpRegistry& ops_;
    ResetHooks hooks_;
};

}  // namespace reboot::storage
