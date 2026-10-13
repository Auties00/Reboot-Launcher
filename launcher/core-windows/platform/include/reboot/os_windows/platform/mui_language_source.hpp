#pragma once

#include <string>
#include <vector>

#include "reboot/ports/ui_language_source.hpp"

namespace rb::os_windows::platform {

// Covers no capability ids; IUiLanguageSource over GetUserPreferredUILanguages, the user's display
// language list, which a process started from the Run key or a scheduled task reads as well.
class MuiLanguageSource final : public ports::IUiLanguageSource {
public:
    // Empty when the call fails.
    [[nodiscard]] std::vector<std::string> preferred_ui_languages() override;
};

}  // namespace rb::os_windows::platform
