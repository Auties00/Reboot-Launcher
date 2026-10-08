#include "reboot/front/front_path.hpp"

#include "authority.hpp"

namespace reboot::front {

namespace {

constexpr std::string_view kRoutePrefix = "/s/";
constexpr std::string_view kRelayPrefix = "/@/";
constexpr std::size_t kKeyDigits = 32;

}  // namespace

std::optional<FrontPath> parse_front_path(std::string_view target) {
    if (!target.starts_with(kRoutePrefix)) return std::nullopt;
    target.remove_prefix(kRoutePrefix.size());
    if (target.size() < kKeyDigits) return std::nullopt;
    const auto key = SessionKey::parse(target.substr(0, kKeyDigits));
    if (!key) return std::nullopt;
    std::string_view rest = target.substr(kKeyDigits);
    if (!rest.empty() && rest.front() != '/' && rest.front() != '?') return std::nullopt;

    FrontPath out{*key, std::nullopt, {}};
    if (rest.starts_with(kRelayPrefix)) {
        rest.remove_prefix(kRelayPrefix.size());
        const std::size_t slash = rest.find('/');
        if (slash == std::string_view::npos) return std::nullopt;
        const std::string_view scheme = rest.substr(0, slash);
        if (scheme != "ws" && scheme != "wss") return std::nullopt;
        rest.remove_prefix(slash + 1);
        const std::size_t end = rest.find_first_of("/?");
        out.relay = origin_from_authority(scheme == "wss" ? net::UrlScheme::Https : net::UrlScheme::Http,
                                          rest.substr(0, end));
        if (!out.relay) return std::nullopt;
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end);
    }
    if (rest.empty() || rest.front() == '?') out.rest = "/";
    out.rest.append(rest);
    return out;
}

}  // namespace reboot::front
