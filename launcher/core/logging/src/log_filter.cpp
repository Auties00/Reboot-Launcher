#include "reboot/logging/log_filter.hpp"

#include <algorithm>

namespace reboot::logging {

bool LogFilter::matches(const LogRecord& record) const noexcept {
    if (record.level < min_level) return false;
    if (!categories.empty() && std::ranges::find(categories, record.category) == categories.end()) return false;
    return !session || record.session == session;
}

}  // namespace reboot::logging
