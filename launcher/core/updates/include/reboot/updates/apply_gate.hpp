#pragma once

#include <optional>
#include <vector>

#include "reboot/foundation/function.hpp"
#include "reboot/updates/activity_probe.hpp"

namespace reboot::updates {

// Capabilities: launcher-updates.check.
// Strand-only. Opens once nothing is live; a host under host.update_policy=manual stays live until stopped.
class ApplyGate {
public:
    // `on_open` runs once: inside arm() when `now` is idle, else inside the first idle update().
    void arm(const ActivitySnapshot& now, UniqueFunction<void()> on_open);
    void disarm() noexcept;
    [[nodiscard]] bool armed() const noexcept;

    void update(const ActivitySnapshot& now);

    // Records what was live when a consented drain began, for the ResumeRecord.
    void drain_started(const ActivitySnapshot& now);
    [[nodiscard]] const std::optional<ActivitySnapshot>& drained() const noexcept { return drained_; }

    // What kept the gate closed at the last arm() or update().
    [[nodiscard]] const std::vector<LiveActivity>& blocking() const noexcept { return blocking_; }

private:
    UniqueFunction<void()> on_open_;
    std::optional<ActivitySnapshot> drained_;
    std::vector<LiveActivity> blocking_;
};

}  // namespace reboot::updates
