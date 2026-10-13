#pragma once

#include <string>
#include <vector>

namespace rb::ports {

// Capabilities: localization.+48.
// The user's preferred UI languages, most preferred first, from the user's own settings, never an
// agent's default environment, which launchd or systemd may leave at C. Each entry is a BCP 47 tag
// or an OS locale name such as "de_DE.UTF-8"; C and POSIX are left out.
class IUiLanguageSource {
public:
    virtual ~IUiLanguageSource() = default;

    // GetUserPreferredUILanguages, CFLocaleCopyPreferredLanguages, or LANGUAGE > LC_ALL > LC_MESSAGES > LANG.
    [[nodiscard]] virtual std::vector<std::string> preferred_ui_languages() = 0;
};

}  // namespace rb::ports
