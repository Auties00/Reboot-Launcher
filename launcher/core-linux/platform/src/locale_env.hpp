#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rb::os_linux::platform {

struct LocaleVariables {
    std::optional<std::string_view> language;
    std::optional<std::string_view> lc_all;
    std::optional<std::string_view> lc_messages;
    std::optional<std::string_view> lang;
};

// LANGUAGE's colon-separated list, then the messages locale: the first of LC_ALL, LC_MESSAGES and
// LANG that is not empty. A C or POSIX messages locale gives nothing, since gettext then ignores
// LANGUAGE too; C and POSIX entries of LANGUAGE are skipped, and repeats kept once.
[[nodiscard]] std::vector<std::string> preferred_languages(const LocaleVariables& variables);

}  // namespace rb::os_linux::platform
