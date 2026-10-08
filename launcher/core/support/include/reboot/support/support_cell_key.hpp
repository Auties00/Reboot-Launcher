#pragma once

#include "reboot/ports/runner.hpp"
#include "reboot/support/support_role.hpp"
#include "reboot/support/version_range.hpp"

namespace reboot::support {

// Host cells always use RunnerKind::Native.
struct SupportCellKey {
    VersionRange range;
    SupportRole role = SupportRole::Play;
    ports::RunnerKind runner = ports::RunnerKind::Native;

    bool operator==(const SupportCellKey&) const = default;
};

}  // namespace reboot::support
