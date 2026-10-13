#pragma once

#include <string>
#include <vector>

#include "reboot/ports/ui_language_source.hpp"

namespace rb::os_linux::platform {

// Covers no capability ids; IUiLanguageSource over the engine's own locale variables, as gettext
// reads them: the client session's on a direct start, the systemd user manager's under systemd.
class LocaleEnvLanguageSource final : public ports::IUiLanguageSource {
public:
    [[nodiscard]] std::vector<std::string> preferred_ui_languages() override;
};

}  // namespace rb::os_linux::platform
