#include "reboot/secrets/secret_scope.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace rb::secrets {

namespace {

std::string lower_ascii(std::string_view text) {
    std::string out(text);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

}  // namespace

SecretScope SecretScope::backend(const HostPort& backend) {
    std::string text = lower_ascii(backend.host);
    if (!backend.port) return SecretScope(std::move(text));
    // An IPv6 literal is bracketed so the port stays separable, as parse_host_port expects.
    if (text.find(':') != std::string::npos) text = "[" + text + "]";
    text += ':';
    text += std::to_string(backend.port->value);
    return SecretScope(std::move(text));
}

SecretScope SecretScope::host_profile(HostProfileId profile) { return SecretScope(format_uuid(profile.value)); }

SecretScope SecretScope::join_request(RequestId request) { return SecretScope(std::to_string(request.value)); }

}  // namespace rb::secrets
