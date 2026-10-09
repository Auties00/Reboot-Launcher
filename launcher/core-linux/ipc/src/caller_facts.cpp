#include "caller_facts.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "decimal_uid.hpp"
#include "display_env_names.hpp"

namespace rb::os_linux::ipc {
namespace {

[[nodiscard]] std::string_view trim_line(std::string_view text) noexcept {
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.remove_suffix(1);
    return text;
}

}  // namespace

ports::CallerContext caller_context_from(const CallerFacts& facts) {
    ports::CallerContext context;
    std::vector<std::string_view> seen;
    for (const std::string_view variable : facts.environment) {
        const std::size_t equals = variable.find('=');
        if (equals == std::string_view::npos || equals == 0) continue;
        const std::string_view name = variable.substr(0, equals);
        const std::string_view value = variable.substr(equals + 1);
        // A later duplicate stays hidden, as getenv sees it.
        if (std::ranges::find(seen, name) != seen.end()) continue;
        seen.push_back(name);
        if (value.empty() || !is_display_env_name(name)) continue;
        context.display_env.emplace_back(std::string(name), std::string(value));
    }

    // The audit session reads 4294967295, (u32)-1, when none is set; parse_decimal_uid refuses it too.
    if (facts.session_id) {
        const std::string_view session = trim_line(*facts.session_id);
        if (parse_decimal_uid(session)) context.os_session = std::string(session);
    }
    const std::optional<u32> login_uid =
        facts.login_uid ? parse_decimal_uid(trim_line(*facts.login_uid)) : std::nullopt;
    context.interactive = login_uid.has_value();
    context.elevated = facts.euid == 0 && login_uid.value_or(facts.uid) != 0;
    return context;
}

}  // namespace rb::os_linux::ipc
