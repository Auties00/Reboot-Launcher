#include "reboot/updates/apply_gate.hpp"

#include <utility>

namespace rb::updates {

void ApplyGate::arm(const ActivitySnapshot& now, UniqueFunction<void()> on_open) {
    on_open_ = std::move(on_open);
    update(now);
}

void ApplyGate::disarm() noexcept {
    on_open_ = nullptr;
    drained_.reset();
    blocking_.clear();
}

bool ApplyGate::armed() const noexcept { return static_cast<bool>(on_open_); }

void ApplyGate::update(const ActivitySnapshot& now) {
    blocking_ = now.live;
    if (!on_open_ || !now.idle()) return;
    // Taken first, so on_open may arm the gate again.
    UniqueFunction<void()> on_open = std::exchange(on_open_, nullptr);
    on_open();
}

void ApplyGate::drain_started(const ActivitySnapshot& now) { drained_ = now; }

}  // namespace rb::updates
