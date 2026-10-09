#include "reboot/browser/deep_link.hpp"

#include <cstddef>
#include <string>

#include "messages.hpp"
#include "reboot/foundation/text.hpp"
#include "text_util.hpp"

namespace rb::browser {

namespace {

// Links come from other programs; the diagnostic keeps only a bounded prefix.
constexpr std::size_t kShownLinkBytes = 128;

[[nodiscard]] std::string_view strip_quotes(std::string_view text) noexcept {
    if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') && text.back() == text.front())
        return text.substr(1, text.size() - 2);
    return text;
}

[[nodiscard]] bool starts_with_scheme(std::string_view text) noexcept {
    const std::size_t length = kDeepLinkScheme.size() + 3;
    return text.size() >= length && iequals_ascii(text.substr(0, kDeepLinkScheme.size()), kDeepLinkScheme) &&
           text.substr(kDeepLinkScheme.size(), 3) == "://";
}

}  // namespace

Result<DeepLink> parse_deep_link(std::string_view text) {
    std::string_view rest = trim_ascii(strip_quotes(trim_ascii(text)));
    if (starts_with_scheme(rest)) {
        rest.remove_prefix(kDeepLinkScheme.size() + 3);
        if (rest.ends_with('/')) rest.remove_suffix(1);
    }
    if (const auto uuid = parse_uuid(rest); uuid && !uuid->is_nil()) return DeepLink{ServerId{*uuid}};
    return make_diag(ErrorDomain::Browser, kInvalidLink)
        .arg("link", sanitize_display_text(text.substr(0, kShownLinkBytes)))
        .kind(ErrorKind::InvalidInput)
        .fail();
}

}  // namespace rb::browser
