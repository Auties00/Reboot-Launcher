#include "reboot/front/upstream_origin.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "authority.hpp"
#include "messages.hpp"
#include "reboot/foundation/text.hpp"

namespace rb::front {

std::string UpstreamOrigin::to_string() const {
    std::string out = scheme == net::UrlScheme::Https ? "https://" : "http://";
    if (host.find(':') != std::string::npos)
        out.append("[").append(host).append("]");
    else
        out.append(host);
    out.append(":").append(std::to_string(port.value));
    return out;
}

Result<UpstreamOrigin> parse_upstream_origin(std::string_view url) {
    constexpr std::array<std::pair<std::string_view, net::UrlScheme>, 4> kSchemes{{
        {"http", net::UrlScheme::Http},
        {"https", net::UrlScheme::Https},
        {"ws", net::UrlScheme::Http},
        {"wss", net::UrlScheme::Https},
    }};
    const auto invalid = [&] { return make_diag(ErrorDomain::Front, msg::kUpstreamInvalid).arg("url", url).fail(); };

    const std::size_t separator = url.find("://");
    if (separator == std::string_view::npos) return invalid();
    const std::string_view scheme_text = url.substr(0, separator);
    const auto scheme =
        std::ranges::find_if(kSchemes, [&](const auto& entry) { return iequals_ascii(entry.first, scheme_text); });
    if (scheme == kSchemes.end()) return invalid();

    const std::string_view rest = url.substr(separator + 3);
    const std::string_view authority = rest.substr(0, rest.find_first_of("/?#"));
    if (authority.find('@') != std::string_view::npos) return invalid();
    auto origin = origin_from_authority(scheme->second, authority);
    if (!origin) return invalid();
    return std::move(*origin);
}

}  // namespace rb::front
