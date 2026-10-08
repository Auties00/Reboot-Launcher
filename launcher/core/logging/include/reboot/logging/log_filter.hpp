#pragma once

#include <optional>
#include <vector>

#include "reboot/foundation/log.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::logging {

// An empty `categories` matches every category; a set `session` excludes records with none.
struct LogFilter {
    LogLevel min_level = LogLevel::Trace;
    std::vector<LogCategory> categories;
    std::optional<SessionId> session;

    [[nodiscard]] bool matches(const LogRecord& record) const noexcept;
};

}  // namespace reboot::logging
