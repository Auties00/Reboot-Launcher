#include "reboot/browser/rbsb_endpoint.hpp"

#include "messages.hpp"
#include "text_util.hpp"

namespace rb::browser {

Result<RbsbExpertOverride> RbsbExpertOverride::parse(std::string_view endpoint, std::optional<NativePath> ca_bundle) {
    const std::string_view text = trim_ascii(endpoint);
    auto parsed = parse_host_port(text);
    if (!parsed)
        return make_diag(ErrorDomain::Browser, kInvalidEndpointOverride)
            .arg("value", text)
            .kind(ErrorKind::InvalidInput)
            .fail();
    if (!parsed->port) parsed->port = kCompiledRbsbPort;
    return RbsbExpertOverride{std::move(*parsed), std::move(ca_bundle)};
}

std::string RbsbEndpoint::status_url() const {
    const bool ipv6_literal = host.find(':') != std::string::npos;
    return "https://" + (ipv6_literal ? '[' + host + ']' : host) + "/status";
}

RbsbEndpoint select_rbsb_endpoint(const std::optional<components::EndpointOverride>& manifest,
                                  const std::optional<RbsbExpertOverride>& expert) {
    if (manifest) return RbsbEndpoint{manifest->host, manifest->port, std::nullopt, EndpointSource::Manifest};
    if (expert)
        return RbsbEndpoint{expert->endpoint.host, expert->endpoint.port.value_or(kCompiledRbsbPort), expert->ca_bundle,
                            EndpointSource::Expert};
    return RbsbEndpoint{std::string(kCompiledRbsbHost), kCompiledRbsbPort, std::nullopt, EndpointSource::Compiled};
}

}  // namespace rb::browser
