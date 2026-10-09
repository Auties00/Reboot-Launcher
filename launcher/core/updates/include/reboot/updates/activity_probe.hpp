#pragma once

#include <optional>
#include <string>
#include <vector>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace rb::updates {

enum class LiveKind : u8 { PlaySession, HostSession, BackendPin, Publication, DetachedOp };

// One thing that keeps the engine from applying an update.
struct LiveActivity {
    LiveKind kind{};
    std::optional<SessionId> session;
    std::optional<HostProfileId> host_profile;
    std::optional<OpId> op;
    // What the session pinned at preflight.
    std::optional<SemVer> payload_version;
    std::optional<std::string> runtime_id;

    bool operator==(const LiveActivity&) const = default;
};

struct ActivitySnapshot {
    std::vector<LiveActivity> live;
    std::vector<contracts::ipc::ClientKind> connected_clients;

    [[nodiscard]] bool idle() const noexcept { return live.empty(); }
};

// Capabilities: launcher-updates.check.
// Implemented by the engine over sessions, backend pins, publications and Detached ops. Strand-only.
class IActivityProbe {
public:
    virtual ~IActivityProbe() = default;

    [[nodiscard]] virtual ActivitySnapshot snapshot() const = 0;
    // Runs on the strand after any live item starts or ends; replaces the earlier listener.
    virtual void set_on_change(UniqueFunction<void()> on_change) = 0;
};

}  // namespace rb::updates
