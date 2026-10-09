#pragma once

#include <optional>

#include "reboot/foundation/types.hpp"
#include "reboot/ux/doc_page.hpp"
#include "reboot/ux/message_text.hpp"

namespace rb::ux {

// Capabilities: onboarding-ux-flows.+24.
// In-app help text with its live values, plus the guide that expands on it.
struct HelpRef {
    MessageText text;
    std::optional<DocPage> doc;
};

// For "other players can't join": names the UDP ports actually bound, one port or a range.
[[nodiscard]] HelpRef port_forwarding_help(u16 first_port, u16 last_port);

}  // namespace rb::ux
