#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace reboot::ux {

struct TemplateInfo {
    // Distinct argument names in order of first appearance, nested ones included.
    std::vector<std::string> args;
    // One entry per construct outside English one/other plurals, e.g. "count: selector few".
    std::vector<std::string> unsupported;
};

// Parses an ICU MessageFormat 1 template with ICU's default apostrophe mode; the error says where it broke.
[[nodiscard]] std::expected<TemplateInfo, std::string> parse_template(std::string_view text);

}  // namespace reboot::ux
