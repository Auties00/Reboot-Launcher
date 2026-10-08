#include "reboot/logging/wine_log_files.hpp"

namespace reboot::logging {

bool routes_to_wine_log(const LogRecord& record) noexcept {
    return record.category == LogCategory::Wine && record.session.has_value();
}

}  // namespace reboot::logging
