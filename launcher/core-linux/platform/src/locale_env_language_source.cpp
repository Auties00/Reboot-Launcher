#include "reboot/os_linux/platform/locale_env_language_source.hpp"

#include <algorithm>

#include "locale_env.hpp"
#include "process_environment.hpp"

namespace rb::os_linux::platform {

namespace {

// "C", "POSIX", and their codeset or modifier forms such as "C.UTF-8".
[[nodiscard]] bool is_c_locale(std::string_view locale) {
    const std::string_view name = locale.substr(0, locale.find_first_of(".@"));
    return name == "C" || name == "POSIX";
}

[[nodiscard]] std::optional<std::string_view> non_empty(std::optional<std::string_view> value) {
    if (value && value->empty()) return std::nullopt;
    return value;
}

}  // namespace

std::vector<std::string> preferred_languages(const LocaleVariables& variables) {
    const std::optional<std::string_view> messages =
        non_empty(variables.lc_all).or_else([&] { return non_empty(variables.lc_messages); }).or_else([&] {
            return non_empty(variables.lang);
        });
    std::vector<std::string> out;
    if (!messages || is_c_locale(*messages)) return out;
    const auto add = [&out](std::string_view entry) {
        if (entry.empty() || is_c_locale(entry) || std::ranges::find(out, entry) != out.end()) return;
        out.emplace_back(entry);
    };
    std::string_view list = variables.language.value_or("");
    while (!list.empty()) {
        const std::size_t end = std::min(list.find(':'), list.size());
        add(list.substr(0, end));
        list.remove_prefix(std::min(end + 1, list.size()));
    }
    add(*messages);
    return out;
}

std::vector<std::string> LocaleEnvLanguageSource::preferred_ui_languages() {
    return preferred_languages(LocaleVariables{.language = env_value("LANGUAGE"),
                                               .lc_all = env_value("LC_ALL"),
                                               .lc_messages = env_value("LC_MESSAGES"),
                                               .lang = env_value("LANG")});
}

std::vector<std::string> LocaleEnvLanguageSource::caller_ui_languages(
    std::span<const contracts::ipc::EnvVar> display_env) {
    if (const std::optional<LocaleVariables> caller = locale_variables_in(display_env)) return preferred_languages(*caller);
    return preferred_ui_languages();
}

}  // namespace rb::os_linux::platform
