#include "reboot/front/upstream_policy.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>

#include "authority.hpp"
#include "reboot/foundation/text.hpp"

namespace reboot::front {

namespace {

constexpr std::string_view kCloudstorageSystem = "/fortnite/api/cloudstorage/system/";
constexpr std::string_view kMatchmakingTicket = "/fortnite/api/game/v2/matchmakingservice/ticket/";
constexpr std::string_view kXmppSection = "OnlineSubsystemMcp.Xmpp";

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

[[nodiscard]] bool starts_with_ascii(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && iequals_ascii(text.substr(0, prefix.size()), prefix);
}

struct XmppSection {
    std::string_view server_addr;
    std::optional<Port> server_port;
    std::optional<bool> use_ssl;
};

// ServerAddr's own port, else the scheme's and ServerPort's; UE builds differ in which one they dial.
void add_section_origins(const XmppSection& section, std::vector<UpstreamOrigin>& out) {
    std::string_view address = section.server_addr;
    if (address.size() >= 2 && address.front() == '"' && address.back() == '"')
        address = address.substr(1, address.size() - 2);
    if (address.empty()) return;

    // UE's bUseSSL defaults to true.
    net::UrlScheme scheme = section.use_ssl.value_or(true) ? net::UrlScheme::Https : net::UrlScheme::Http;
    if (const std::size_t separator = address.find("://"); separator != std::string_view::npos) {
        const std::string_view scheme_text = address.substr(0, separator);
        if (iequals_ascii(scheme_text, "ws"))
            scheme = net::UrlScheme::Http;
        else if (iequals_ascii(scheme_text, "wss"))
            scheme = net::UrlScheme::Https;
        else
            return;
        address.remove_prefix(separator + 3);
    }
    const std::string_view authority = address.substr(0, address.find_first_of("/?"));
    const auto split = split_authority(authority);
    if (!split) return;
    auto host = normalize_host(split->host);
    if (!host) return;
    if (split->port) {
        out.push_back({scheme, std::move(*host), *split->port});
        return;
    }
    out.push_back({scheme, *host, default_port(scheme)});
    if (section.server_port) out.push_back({scheme, std::move(*host), *section.server_port});
}

[[nodiscard]] std::vector<UpstreamOrigin> xmpp_origins(std::string_view ini) {
    std::vector<UpstreamOrigin> out;
    std::optional<XmppSection> section;
    while (!ini.empty()) {
        const std::size_t newline = ini.find('\n');
        const std::string_view line = trim(ini.substr(0, newline));
        ini = newline == std::string_view::npos ? std::string_view{} : ini.substr(newline + 1);
        if (line.starts_with('[') && line.ends_with(']')) {
            if (section) add_section_origins(*section, out);
            section.reset();
            if (starts_with_ascii(line.substr(1, line.size() - 2), kXmppSection)) section.emplace();
            continue;
        }
        const std::size_t eq = line.find('=');
        if (!section || eq == std::string_view::npos) continue;
        const std::string_view key = trim(line.substr(0, eq));
        const std::string_view value = trim(line.substr(eq + 1));
        if (iequals_ascii(key, "ServerAddr")) {
            section->server_addr = value;
        } else if (iequals_ascii(key, "ServerPort")) {
            if (const auto port = parse_port(value)) section->server_port = *port;
        } else if (iequals_ascii(key, "bUseSSL")) {
            section->use_ssl = iequals_ascii(value, "true") || value == "1";
        }
    }
    if (section) add_section_origins(*section, out);
    return out;
}

[[nodiscard]] std::vector<UpstreamOrigin> service_url_origins(std::string_view json) {
    boost::system::error_code error;
    const boost::json::value ticket = boost::json::parse(json, error);
    if (error || !ticket.is_object()) return {};
    const boost::json::value* service_url = ticket.get_object().if_contains("serviceUrl");
    if (!service_url || !service_url->is_string()) return {};
    auto origin = parse_upstream_origin(std::string_view(service_url->get_string()));
    if (!origin) return {};
    return {std::move(*origin)};
}

}  // namespace

UpstreamPolicy::UpstreamPolicy(UpstreamOrigin backend)
    : backend_(std::move(backend)), backend_on_loopback_(is_loopback_host(backend_.host)) {}

bool UpstreamPolicy::allows(const UpstreamOrigin& target) const {
    return target == backend_ || std::ranges::find(learned_, target) != learned_.end();
}

bool UpstreamPolicy::allows_connect(const UpstreamOrigin& target, const Endpoint& resolved,
                                    std::span<const Port> front_ports) const {
    const bool local = reaches_this_machine(resolved.address);
    if (local && std::ranges::find(front_ports, resolved.port) != front_ports.end()) return false;
    return allows(target) && (!local || backend_on_loopback_);
}

bool UpstreamPolicy::learns_from(std::string_view path) noexcept {
    path = path.substr(0, path.find('?'));
    return (path.starts_with(kCloudstorageSystem) && path.size() > kCloudstorageSystem.size()) ||
           path.starts_with(kMatchmakingTicket);
}

std::vector<UpstreamOrigin> UpstreamPolicy::learn(std::string_view path, std::span<const u8> decoded_body) {
    if (decoded_body.size() > kLearnBodyCap || !learns_from(path)) return {};
    path = path.substr(0, path.find('?'));
    const std::string_view text(reinterpret_cast<const char*>(decoded_body.data()), decoded_body.size());

    std::vector<UpstreamOrigin> found;
    if (path.starts_with(kCloudstorageSystem))
        found = xmpp_origins(text);
    else
        found = service_url_origins(text);

    std::vector<UpstreamOrigin> added;
    for (UpstreamOrigin& origin : found) {
        if (allows(origin)) continue;
        learned_.push_back(origin);
        added.push_back(std::move(origin));
    }
    return added;
}

}  // namespace reboot::front
