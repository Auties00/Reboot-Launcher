#pragma once

#include <span>
#include <string>
#include <vector>

#include "reboot/contracts/ipc.hpp"

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

    // For one client, given its Hello display_env. Linux reads the session's locale variables
    // there, since a systemd-started engine has the user manager's; by default, and when they are
    // absent, preferred_ui_languages().
    [[nodiscard]] virtual std::vector<std::string> caller_ui_languages(
        std::span<const contracts::ipc::EnvVar> display_env) {
        (void)display_env;
        return preferred_ui_languages();
    }
};

}  // namespace rb::ports
