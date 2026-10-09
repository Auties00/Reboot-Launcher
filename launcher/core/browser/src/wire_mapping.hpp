#pragma once

#include <chrono>

#include "reboot/browser/view_spec.hpp"
#include "reboot/foundation/types.hpp"
#include "wire/messages.hpp"

namespace reboot::browser {

// Our enums mirror rbsb/1's values one to one.
[[nodiscard]] constexpr sb::wire::Region to_wire(Region region) noexcept {
    return static_cast<sb::wire::Region>(static_cast<u32>(region));
}
[[nodiscard]] constexpr Region from_wire(sb::wire::Region region) noexcept {
    const auto value = static_cast<u32>(region);
    return value < sb::wire::kRegionCount ? static_cast<Region>(value) : Region::All;
}

[[nodiscard]] constexpr sb::wire::PasswordFilter to_wire(PasswordFilter filter) noexcept {
    switch (filter) {
        case PasswordFilter::Without: return sb::wire::PasswordFilter::none;
        case PasswordFilter::Only: return sb::wire::PasswordFilter::only;
        case PasswordFilter::Any: break;
    }
    return sb::wire::PasswordFilter::any;
}

[[nodiscard]] constexpr sb::wire::Sort to_wire(ServerSort sort) noexcept {
    switch (sort) {
        case ServerSort::Newest: return sb::wire::Sort::newest;
        case ServerSort::Name: return sb::wire::Sort::name;
        case ServerSort::Players: break;
    }
    return sb::wire::Sort::players;
}

// The windows Subscribe supports: up to 50, else 200, never above the edge's max_window.
[[nodiscard]] constexpr u32 effective_window(u32 requested, u32 max_window) noexcept {
    const u32 rounded = requested <= kSmallWindow ? kSmallWindow : kLargeWindow;
    return max_window != 0 && rounded > max_window ? max_window : rounded;
}

// Edge milliseconds since the Unix epoch, moved onto the local clock.
[[nodiscard]] inline std::chrono::system_clock::time_point local_time(u64 edge_ms,
                                                                    std::chrono::milliseconds clock_offset) {
    const std::chrono::milliseconds edge{static_cast<std::chrono::milliseconds::rep>(edge_ms)};
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(edge - clock_offset));
}

}  // namespace reboot::browser
