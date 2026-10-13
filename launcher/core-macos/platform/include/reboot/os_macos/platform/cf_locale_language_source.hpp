#pragma once

#include <string>
#include <vector>

#include "reboot/ports/ui_language_source.hpp"

namespace rb::os_macos::platform {

// Covers no capability ids; IUiLanguageSource over CFLocaleCopyPreferredLanguages, the user's
// language list from System Settings, which a launchd agent in the user's domain reads as well.
class CfLocaleLanguageSource final : public ports::IUiLanguageSource {
public:
    [[nodiscard]] std::vector<std::string> preferred_ui_languages() override;
};

}  // namespace rb::os_macos::platform
