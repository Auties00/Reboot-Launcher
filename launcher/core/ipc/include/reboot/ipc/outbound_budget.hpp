#pragma once

#include <cstddef>
#include <optional>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot {
class IClock;
}

namespace reboot::ipc {

// Covers no capability ids. EventBatch bytes sent and not yet credited back; IByteStream reports
// no queue, and the client credits every event as soon as it is taken. Strand-only.
class OutboundBudget {
public:
    enum class Verdict : u8 { Resync, Disconnect };

    explicit OutboundBudget(const IClock& clock, std::size_t limit = contracts::ipc::kOutboundBudget) noexcept
        : clock_(clock), limit_(limit) {}

    [[nodiscard]] bool fits(std::size_t bytes) const noexcept { return used_ + bytes <= limit_; }
    void charge(std::size_t bytes) noexcept { used_ += bytes; }
    void refund(std::size_t bytes) noexcept { used_ -= bytes < used_ ? bytes : used_; }
    [[nodiscard]] std::size_t used() const noexcept { return used_; }

    // Resync on the first miss, Disconnect on a second within kSlowConsumerWindow.
    [[nodiscard]] Verdict overflow();

private:
    const IClock& clock_;
    std::size_t limit_;
    std::size_t used_ = 0;
    std::optional<SteadyTime> last_overflow_;
};

}  // namespace reboot::ipc
