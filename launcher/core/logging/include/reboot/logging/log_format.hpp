#pragma once

#include <string>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/log.hpp"

namespace reboot::logging {

[[nodiscard]] std::string_view level_name(LogLevel level) noexcept;
[[nodiscard]] std::string_view category_name(LogCategory category) noexcept;
[[nodiscard]] LogLevel level_for(Severity severity) noexcept;

// UTC millisecond timestamp, level, category, session, text. Continuation lines are indented and
// other control characters escaped, so record text can never forge a line of its own.
[[nodiscard]] std::string format_log_line(const LogRecord& record);

// The id, args, detail, OS error and causes; never the English template.
[[nodiscard]] std::string format_diagnostic(const Diagnostic& diag);

}  // namespace reboot::logging
