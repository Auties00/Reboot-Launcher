#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::builds {

struct FindOptions {
    // Directories deeper than this below the root are not entered.
    u32 max_depth = 8;
};

struct FoundFile {
    // Index into the `names` passed to find().
    std::size_t name_index = 0;
    // Relative to the root, with the casing found on disk.
    NativePath relative;

    bool operator==(const FoundFile&) const = default;
};

// A directory the walk could not read; the walk goes on without it.
struct WalkError {
    NativePath path;
    SystemError os_error;

    bool operator==(const WalkError&) const = default;
};

struct FindResult {
    // Entries are visited sorted by name, so the order is deterministic.
    std::vector<FoundFile> files;
    std::vector<WalkError> errors;
    bool depth_capped = false;
};

// Capabilities: game-builds.file-search.
// One walk for many names, ASCII case-folded. Blocking; runs on the WorkerPool. Reparse points are
// never followed, so no cycle can form. Fails only on an unreadable root or with builds.cancelled.
class FileFinder {
public:
    explicit FileFinder(FindOptions options = {}) noexcept : options_(options) {}

    [[nodiscard]] Result<FindResult> find(const NativePath& root, std::span<const std::string_view> names,
                                          const CancelToken& token) const;

private:
    FindOptions options_;
};

}  // namespace reboot::builds
