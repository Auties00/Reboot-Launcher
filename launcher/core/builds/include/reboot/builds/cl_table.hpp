#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace rb::builds {

struct ClTableEntry {
    u32 changelist = 0;
    // Canonical GameVersion text.
    std::string_view version;
};

// The compiled-in engine CL map for "Cert"/"Next" builds, with the labels fn-releases contradicts
// fixed (4461277 is 6.02). It changes only with a release.
[[nodiscard]] std::span<const ClTableEntry> compiled_cl_entries() noexcept;

// Capabilities: game-builds.version-detection.
// Exact lookups only, so a CL between two entries never resolves.
class ClTable {
public:
    ClTable() noexcept : entries_(compiled_cl_entries()) {}
    // `entries` must be sorted by changelist and outlive the table.
    explicit ClTable(std::span<const ClTableEntry> entries) noexcept : entries_(entries) {}

    [[nodiscard]] std::optional<GameVersion> lookup(Changelist changelist) const;
    [[nodiscard]] std::span<const ClTableEntry> entries() const noexcept { return entries_; }

private:
    std::span<const ClTableEntry> entries_;
};

}  // namespace rb::builds
