#pragma once

#include <optional>
#include <vector>

#include "reboot/base/types.hpp"
#include "reboot/foundation/log.hpp"

namespace rb::logging {

// An empty `categories` matches every category; a set `session` excludes records with none.
struct LogFilter {
    LogLevel min_level = LogLevel::Trace;
    std::vector<LogCategory> categories;
    std::optional<SessionId> session;

    [[nodiscard]] bool matches(const LogRecord& record) const noexcept;
};

}  // namespace rb::logging
