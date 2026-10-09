#pragma once

#include <vector>

#include "reboot/ux/language_tag.hpp"

namespace rb::ux {

// Capabilities: localization.+48.
// In preference order, from the user's locale, never the agent's environment, which launchd or systemd may leave at C.
class IUiLanguageSource {
public:
    virtual ~IUiLanguageSource() = default;

    // GetUserPreferredUILanguages, CFLocaleCopyPreferredLanguages, or LANGUAGE > LC_ALL > LC_MESSAGES > LANG.
    [[nodiscard]] virtual std::vector<LanguageTag> preferred_ui_languages() = 0;
};

}  // namespace rb::ux
