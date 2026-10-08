#pragma once

#include <optional>
#include <string_view>

#include "reboot/foundation/types.hpp"

namespace reboot::builds {

enum class LibraryChange : u8 { Added, Updated, Removed, SelectionChanged };

// EventKind::LibraryChanged. One coalescing key for the whole library, so a lagging subscriber
// may see only the last change and re-lists.
struct LibraryChangedEvent {
    LibraryChange change = LibraryChange::Updated;
    std::optional<BuildId> build;
    u64 revision = 0;
};

inline constexpr std::string_view kLibraryCoalesceKey = "library";

}  // namespace reboot::builds
