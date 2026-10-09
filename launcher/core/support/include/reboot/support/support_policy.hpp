#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "reboot/support/auto_server_verdict.hpp"
#include "reboot/support/host_inputs.hpp"
#include "reboot/support/support_cell.hpp"
#include "reboot/support/support_inputs.hpp"
#include "reboot/support/support_query.hpp"
#include "reboot/support/support_verdict.hpp"

namespace rb::support {

// game-builds.+3: rates builds per version range x role x runner. Owned by the engine; UIs only
// render it. Every rule that applies adds a reason, and the tier is the worst of them:
// - unknown version: Blocked;
// - above kMaxSupportedVersion: Blocked, or Untested for an imported build with the opt-in;
// - a play runner without a pin on this OS, or a host query on a Wine runner: Blocked;
// - host without a described server, or outside every range it describes: Blocked;
// - custom auth DLL (provider Custom) or external backend: Untested;
// - the cell: Tested only when its newest record for the current inputs passed.
// So the opt-in lifts only the cap, never a Blocked runner or server.
class SupportPolicy {
public:
    explicit SupportPolicy(SupportInputs inputs) : inputs_(std::move(inputs)) {}

    // The engine calls this when the payload, backend content, a runtime pin or the evidence
    // changes; services keep their reference to this policy.
    void set_inputs(SupportInputs inputs) { inputs_ = std::move(inputs); }

    // When several cells contain the build, the best-rated one applies.
    [[nodiscard]] SupportVerdict evaluate(const SupportQuery& query) const;

    // `play.role` must be Play; the host side is the same build on the Native runner with
    // `play.server`.
    [[nodiscard]] AutoServerVerdict evaluate_with_auto_server(const SupportQuery& play) const;

    // One cell per evidence cell of this OS and per range `server` describes. A version outside
    // every cell is Untested up to kMaxSupportedVersion and Blocked above it.
    [[nodiscard]] std::vector<SupportCell> cells(const std::optional<HostInputs>& server) const;

    [[nodiscard]] const SupportInputs& inputs() const noexcept { return inputs_; }

private:
    [[nodiscard]] SupportCell rate_cell(const SupportCellKey& key, const std::optional<HostInputs>& server) const;

    SupportInputs inputs_;
};

}  // namespace rb::support
