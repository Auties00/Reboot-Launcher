#pragma once

#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ux/language_tag.hpp"

namespace rb::ux {

enum class AppLink : u8 { BugReport, Releases, Discord };

struct AppLinkEntry {
    AppLink link{};
    MessageId label;
    std::string url;
};

// Capabilities: onboarding-ux-flows.+24, onboarding-ux-flows.first-run-tour.
// Product links every UI shows; the language is passed per call because ui.language changes at runtime.
class AppLinks {
public:
    [[nodiscard]] std::string url(AppLink link, const LanguageTag& language) const;
    [[nodiscard]] std::vector<AppLinkEntry> list(const LanguageTag& language) const;
};

}  // namespace rb::ux
