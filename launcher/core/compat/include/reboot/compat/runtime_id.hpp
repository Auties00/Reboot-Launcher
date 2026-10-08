#pragma once

#include <compare>
#include <string>
#include <string_view>

namespace reboot::compat {

// The manifest id of one runtime component; a newer build of a runtime gets a new id.
struct RuntimeId {
    std::string value;

    auto operator<=>(const RuntimeId&) const = default;
};

// Natural order over digit runs, so "GE-Proton11-7" sorts after "GE-Proton10-25" and "11.0.2"
// after "11.0". Decides whether a prefix is upgraded or downgraded.
[[nodiscard]] std::strong_ordering compare_runtime_versions(std::string_view a, std::string_view b) noexcept;

}  // namespace reboot::compat
