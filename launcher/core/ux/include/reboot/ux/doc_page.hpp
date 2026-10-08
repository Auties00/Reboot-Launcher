#pragma once

#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ux/language_tag.hpp"

namespace reboot::ux {

// Capabilities: onboarding-ux-flows.+24.
enum class DocPage : u8 {
    // Platform-neutral port forwarding and VPN guide; the ports come from HelpRef's text.
    PortForwardingGuide,
};

// The page in `language` when it is translated, otherwise the en page.
[[nodiscard]] std::string resolve_doc_url(DocPage page, const LanguageTag& language);

[[nodiscard]] MessageId doc_page_label(DocPage page);

}  // namespace reboot::ux
