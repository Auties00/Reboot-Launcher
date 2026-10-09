#include "reboot/front/loopback_authority.hpp"

#include "reboot/foundation/text.hpp"

namespace rb::front {

namespace {

constexpr std::string_view kHttpScheme = "http://";

[[nodiscard]] bool is_loopback_name(std::string_view host) {
    return host == "127.0.0.1" || iequals_ascii(host, "localhost");
}

// A loopback name with `port`, or with no port when `portless_ok`.
[[nodiscard]] bool is_listener(std::string_view authority, Port port, bool portless_ok) {
    const std::size_t colon = authority.find(':');
    if (!is_loopback_name(authority.substr(0, colon))) return false;
    if (colon == std::string_view::npos) return portless_ok;
    const auto parsed = parse_port(authority.substr(colon + 1));
    return parsed && *parsed == port;
}

}  // namespace

bool LoopbackAuthority::allows_host(std::string_view host_header) const { return is_listener(host_header, port, true); }

bool LoopbackAuthority::allows_origin(std::optional<std::string_view> origin_header) const {
    if (!origin_header) return true;
    const std::string_view origin = *origin_header;
    if (origin.size() >= kHttpScheme.size() && iequals_ascii(origin.substr(0, kHttpScheme.size()), kHttpScheme))
        return is_listener(origin.substr(kHttpScheme.size()), port, port == Port{80});
    return is_listener(origin, port, true);
}

}  // namespace rb::front
