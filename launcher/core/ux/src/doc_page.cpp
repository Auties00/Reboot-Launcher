#include "reboot/ux/doc_page.hpp"

#include <array>

#include "links.hpp"
#include "messages.hpp"

namespace reboot::ux {

namespace {

constexpr std::array<LocalizedUrl, 1> kPortForwardingGuide{
    LocalizedUrl{"en", "https://github.com/Auties00/Reboot-Launcher/blob/master/documentation/en/PortForwarding.md"}};

}  // namespace

std::string resolve_doc_url(DocPage page, const LanguageTag& language) {
    switch (page) {
        case DocPage::PortForwardingGuide: return localized_url(kPortForwardingGuide, language);
    }
    return {};
}

MessageId doc_page_label(DocPage page) {
    switch (page) {
        case DocPage::PortForwardingGuide: return msg::kLinkPortForwardingGuide;
    }
    return msg::kLinkPortForwardingGuide;
}

}  // namespace reboot::ux
