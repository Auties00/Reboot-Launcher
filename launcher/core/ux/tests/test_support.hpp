#pragma once

#include <utility>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ux/guidance_state_store.hpp"

namespace rb::ux::test {

// Write-through store whose next write can be made to fail.
class MemoryGuidanceStore final : public IGuidanceStateStore {
public:
    [[nodiscard]] const GuidanceState& current() const override { return state_; }

    Result<void> replace(GuidanceState state) override {
        ++writes;
        if (fail_next) {
            fail_next = false;
            return make_diag(ErrorDomain::Storage, MessageId{"storage.write_failed"}).fail();
        }
        state_ = std::move(state);
        return {};
    }

    bool fail_next = false;
    int writes = 0;

private:
    GuidanceState state_;
};

template <class IdType>
[[nodiscard]] IdType make_id(u8 seed) {
    IdType id;
    id.value.bytes[15] = seed;
    return id;
}

}  // namespace rb::ux::test
