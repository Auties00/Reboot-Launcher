#pragma once

#include <chrono>
#include <span>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace rb::updates {

// Starts of `to` allowed before the Linux shim rolls back, or, with no `previous`, the engine gives up.
inline constexpr u32 kMaxUpdateAttempts = 2;
// Clients wait and retry while the marker is younger than this, then start an engine anyway.
inline constexpr std::chrono::seconds kUpdateMarkerTimeout{120};

// state/update-in-progress: written before the restart, removed once `to` passes its IPC self-test.
struct PendingUpdateMarker {
    SemVer from;
    SemVer to;
    // Starts of `to`: the Linux shim counts each before exec, begin_startup counts them elsewhere.
    u32 attempts = 0;
    // Refreshed with each count, so the clients' wait runs from the latest start.
    std::chrono::system_clock::time_point started_at;

    bool operator==(const PendingUpdateMarker&) const = default;
};

// A small JSON object, so the Linux shim can read it without this package.
[[nodiscard]] std::vector<u8> encode_marker(const PendingUpdateMarker& marker);
// updates.marker_malformed naming the field.
[[nodiscard]] Result<PendingUpdateMarker> parse_marker(std::span<const u8> bytes);

enum class MarkerVerdict : u8 {
    SelfTest,    // running `to` within kMaxUpdateAttempts: self-test, then confirm
    NotApplied,  // running `from`: the apply failed or the shim rolled back
    GiveUp,      // running `to` past kMaxUpdateAttempts with nothing to roll back to
    Foreign,     // running neither version
};

[[nodiscard]] MarkerVerdict judge_marker(const PendingUpdateMarker& marker, const SemVer& running);

}  // namespace rb::updates
