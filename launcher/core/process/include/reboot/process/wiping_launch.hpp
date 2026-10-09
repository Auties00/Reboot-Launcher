#pragma once

#include <utility>

#include "reboot/ports/process.hpp"

namespace rb::process {

// Covers no capability ids. A ports::ProcessLaunch that overwrites its environment values when it
// is destroyed, so a launch copy carrying REBOOT_CTL_TOKEN never outlives the spawn. Move-only.
class WipingLaunch {
public:
    explicit WipingLaunch(ports::ProcessLaunch launch) noexcept : launch_(std::move(launch)) {}
    WipingLaunch(WipingLaunch&& other) noexcept = default;
    WipingLaunch& operator=(WipingLaunch&& other) noexcept;
    WipingLaunch(const WipingLaunch&) = delete;
    WipingLaunch& operator=(const WipingLaunch&) = delete;
    ~WipingLaunch();

    [[nodiscard]] const ports::ProcessLaunch& get() const noexcept { return launch_; }

private:
    ports::ProcessLaunch launch_;
};

}  // namespace rb::process
