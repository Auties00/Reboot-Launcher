#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/logging/log_file_names.hpp"

namespace rb::logging {

// Capabilities: logging-diagnostics.log-and-errors.
// One budget over every log kind in the directory: a file goes when any limit is exceeded.
struct RetentionPolicy {
    // Groups, each with all its session parts and Wine logs; Proton logs count only toward age and bytes.
    std::size_t max_sessions = 20;
    std::chrono::days max_age{14};
    u64 max_total_bytes = 256ull << 20;
};

struct LogFileInfo {
    NativePath path;
    LogFileKind kind{};
    std::optional<LogFileGroup> group;
    u64 size = 0;
    std::chrono::system_clock::time_point modified;
};

// Oldest first. Files in `in_use` are never selected but still count toward the limits.
[[nodiscard]] std::vector<NativePath> select_expired(std::span<const LogFileInfo> files, const RetentionPolicy& policy,
                                                     std::chrono::system_clock::time_point now,
                                                     std::span<const NativePath> in_use);

}  // namespace rb::logging
