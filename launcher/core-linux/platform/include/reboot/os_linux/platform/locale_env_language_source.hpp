#pragma once

#include <span>
#include <string>
#include <vector>

#include "reboot/ports/ui_language_source.hpp"

namespace rb::os_linux::platform {

// Covers no capability ids; IUiLanguageSource over locale variables, as gettext reads them.
class LocaleEnvLanguageSource final : public ports::IUiLanguageSource {
public:
    // The engine's own: the client session's on a self-spawn, the user manager's under systemd.
    [[nodiscard]] std::vector<std::string> preferred_ui_languages() override;
    // The client's LANGUAGE, LC_ALL, LC_MESSAGES and LANG from its display_env when it carries any,
    // so a systemd-started engine answers in the session's language; else preferred_ui_languages().
    [[nodiscard]] std::vector<std::string> caller_ui_languages(
        std::span<const contracts::ipc::EnvVar> display_env) override;
};

}  // namespace rb::os_linux::platform
