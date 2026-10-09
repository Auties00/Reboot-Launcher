#include "reboot/ux/help_ref.hpp"

#include <algorithm>

#include "messages.hpp"

namespace rb::ux {

HelpRef port_forwarding_help(u16 first_port, u16 last_port) {
    const u16 low = std::min(first_port, last_port);
    const u16 high = std::max(first_port, last_port);
    HelpRef help{.doc = DocPage::PortForwardingGuide};
    if (low == high) {
        help.text = {msg::kHelpPortForwardingPort, {{"port", Arg{u64{low}}}}};
    } else {
        help.text = {msg::kHelpPortForwardingPortRange, {{"first_port", Arg{u64{low}}}, {"last_port", Arg{u64{high}}}}};
    }
    return help;
}

}  // namespace rb::ux
