#pragma once

#include "reboot/foundation/function.hpp"
#include "reboot/integration/purge_blockers.hpp"
#include "reboot/integration/purge_scope.hpp"

namespace reboot::integration {

// Supplied by the engine, which owns sessions, ops, the backend lease and the logger; all run on the strand.
struct PurgeHooks {
    // Live sessions always block; the backend blocks BackendData and All.
    UniqueFunction<PurgeBlockers(PurgeScope)> find_blockers;
    // Lets owners close files they hold in the scope, as the logger its current files.
    UniqueFunction<void(PurgeScope, UniqueFunction<void()> ready)> prepare;
    // After deletion, success or not, so owners reopen files and drop cached state.
    UniqueFunction<void(PurgeScope)> on_purged;
};

}  // namespace reboot::integration
