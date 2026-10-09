#pragma once

#include <span>
#include <string>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/ux/app_links.hpp"
#include "reboot/ux/language_tag.hpp"

namespace reboot::ux {

struct LocalizedUrl {
    std::string_view language;
    std::string_view url;
};

// The url for `language` by RFC 4647 Lookup, else the en one; `urls` always holds an en entry.
[[nodiscard]] std::string localized_url(std::span<const LocalizedUrl> urls, const LanguageTag& language);

[[nodiscard]] MessageId app_link_label(AppLink link);

}  // namespace reboot::ux
