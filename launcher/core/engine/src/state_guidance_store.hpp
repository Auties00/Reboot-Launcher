#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/storage/state_document.hpp"
#include "reboot/ux/guidance_state.hpp"
#include "reboot/ux/guidance_state_store.hpp"

namespace reboot::engine {

// Capabilities: none. Strand-only; ux's guidance over state/state.json. StateDocument has no member
// for notice args, so they stay under "guidance" in its unknown members until storage adds one.
class StateGuidanceStore final : public ux::IGuidanceStateStore {
public:
    explicit StateGuidanceStore(storage::DocumentStore<storage::StateDocument>& state);
    StateGuidanceStore(const StateGuidanceStore&) = delete;
    StateGuidanceStore& operator=(const StateGuidanceStore&) = delete;

    [[nodiscard]] const ux::GuidanceState& current() const override { return current_; }
    Result<void> replace(ux::GuidanceState state) override;

private:
    storage::DocumentStore<storage::StateDocument>& state_;
    ux::GuidanceState current_;
};

}  // namespace reboot::engine
