#include "reboot/ipc/outbound_budget.hpp"

#include "reboot/foundation/clock.hpp"

namespace reboot::ipc {

OutboundBudget::Verdict OutboundBudget::overflow() {
    const SteadyTime now = clock_.steady_now();
    if (last_overflow_ && now - *last_overflow_ < contracts::ipc::kSlowConsumerWindow) return Verdict::Disconnect;
    last_overflow_ = now;
    return Verdict::Resync;
}

}  // namespace reboot::ipc
