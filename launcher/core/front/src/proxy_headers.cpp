#include "proxy_headers.hpp"

#include "reboot/foundation/text.hpp"
#include "reboot/front/reboot_headers.hpp"

namespace reboot::front {

namespace {

[[nodiscard]] std::string bracketed(const std::string& host) {
    return host.find(':') != std::string::npos ? "[" + host + "]" : host;
}

}  // namespace

std::string host_header(const UpstreamOrigin& origin) {
    std::string out = bracketed(origin.host);
    if (origin.port != default_port(origin.scheme)) out.append(":").append(std::to_string(origin.port.value));
    return out;
}

std::string relay_segment(const UpstreamOrigin& origin) {
    std::string out = origin.scheme == net::UrlScheme::Https ? "/@/wss/" : "/@/ws/";
    return out.append(bracketed(origin.host)).append(":").append(std::to_string(origin.port.value));
}

bool is_reboot_header(std::string_view name) noexcept {
    return name.size() >= kRebootHeaderPrefix.size() &&
           iequals_ascii(name.substr(0, kRebootHeaderPrefix.size()), kRebootHeaderPrefix);
}

std::optional<std::string> rewrite_location(std::string_view location, const UpstreamOrigin& upstream,
                                            const FrontBase& base) {
    if (location.starts_with('/')) {
        if (location.starts_with("//") || base.prefix.empty()) return std::nullopt;
        return base.prefix + std::string(location);
    }
    const std::size_t separator = location.find("://");
    if (separator == std::string_view::npos) return std::nullopt;
    const Result<UpstreamOrigin> origin = parse_upstream_origin(location);
    if (!origin || *origin != upstream) return std::nullopt;

    const std::string_view after = location.substr(separator + 3);
    const std::size_t rest_start = after.find_first_of("/?#");
    const std::string_view rest = rest_start == std::string_view::npos ? std::string_view{} : after.substr(rest_start);
    std::string out = base.origin + base.prefix;
    if (!rest.starts_with('/')) out.push_back('/');
    out.append(rest);
    return out;
}

}  // namespace reboot::front
