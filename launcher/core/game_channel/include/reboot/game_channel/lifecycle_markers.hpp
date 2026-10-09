#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "reboot/foundation/types.hpp"

namespace reboot::game_channel {

enum class LegacyMarker : u8 { LoginCompleted, Shutdown, CorruptBuild, AuthFailure, CannotConnect };

// Plain substrings: every one must occur somewhere in the line.
struct MarkerPattern {
    LegacyMarker marker{};
    std::span<const std::string_view> substrings;
};

// Covers game-launch.output-monitoring.
// Versioned marker data owned by the core. The first matching pattern wins, so the end of a session
// outranks its progress: Shutdown, CorruptBuild, AuthFailure, CannotConnect, then LoginCompleted.
struct LifecycleMarkers {
    u32 version = 0;
    std::span<const MarkerPattern> patterns;

    // `line` is a whole line from process::LineReader, never a pipe chunk.
    [[nodiscard]] std::optional<LegacyMarker> match(std::string_view line) const noexcept;
};

[[nodiscard]] const LifecycleMarkers& builtin_lifecycle_markers() noexcept;

}  // namespace reboot::game_channel
