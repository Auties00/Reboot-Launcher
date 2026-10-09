#pragma once

#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::builds {

// Capabilities: game-builds.library.
// Strand-only. The in-use guard for removal and relocation; the sessions package implements it,
// since a play or host session pins its build when it starts.
class IBuildUsage {
public:
    virtual ~IBuildUsage() = default;

    [[nodiscard]] virtual std::vector<SessionId> sessions_using(BuildId build) const = 0;
    // Stops every session using the build with the usual grace; `done` runs on the strand once
    // all of them ended.
    virtual void stop_sessions_using(BuildId build, UniqueFunction<void(Result<void>)> done) = 0;
};

}  // namespace rb::builds
