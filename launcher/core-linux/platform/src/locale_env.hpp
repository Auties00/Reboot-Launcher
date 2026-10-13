#pragma once

#include <optional>
#include <span>
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

// The locale variables among a client's display_env entries, the first of each name, as getenv
// sees them; nullopt when none is set and non-empty. `Entry` has string `name` and `value`.
template <class Entry>
[[nodiscard]] std::optional<LocaleVariables> locale_variables_in(std::span<const Entry> entries) {
    LocaleVariables found;
    bool any = false;
    const auto take = [&any](std::optional<std::string_view>& slot, const Entry& entry) {
        if (slot) return;
        slot = std::string_view{entry.value};
        any = any || !entry.value.empty();
    };
    for (const Entry& entry : entries) {
        if (entry.name == "LANGUAGE")
            take(found.language, entry);
        else if (entry.name == "LC_ALL")
            take(found.lc_all, entry);
        else if (entry.name == "LC_MESSAGES")
            take(found.lc_messages, entry);
        else if (entry.name == "LANG")
            take(found.lang, entry);
    }
    if (!any) return std::nullopt;
    return found;
}

// LANGUAGE's colon-separated list, then the messages locale: the first of LC_ALL, LC_MESSAGES and
// LANG that is not empty. A C or POSIX messages locale gives nothing, since gettext then ignores
// LANGUAGE too; C and POSIX entries of LANGUAGE are skipped, and repeats kept once.
[[nodiscard]] std::vector<std::string> preferred_languages(const LocaleVariables& variables);

}  // namespace rb::os_linux::platform
