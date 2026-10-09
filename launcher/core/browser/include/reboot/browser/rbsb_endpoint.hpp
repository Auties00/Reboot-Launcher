#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "reboot/components/endpoint_override.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::browser {

// A DNS name only: an IP address is never compiled in.
inline constexpr std::string_view kCompiledRbsbHost = "sb.rebootfn.org";
inline constexpr Port kCompiledRbsbPort{443};

// The engine's own environment, read at startup when the expert setting is unset.
inline constexpr std::string_view kRbsbEndpointEnv = "REBOOT_RBSB_ENDPOINT";
inline constexpr std::string_view kRbsbCaEnv = "REBOOT_RBSB_CA";

enum class EndpointSource : u8 { Compiled, Manifest, Expert };

// For staging edges and self-hosters: "host" or "host:port", plus an optional PEM bundle file that
// replaces the system trust store. The bundle is a public certificate, so it is a path, not a secret.
struct RbsbExpertOverride {
    HostPort endpoint;
    std::optional<NativePath> ca_bundle;

    bool operator==(const RbsbExpertOverride&) const = default;

    // Fails with browser.invalid_endpoint_override; the port defaults to 443.
    [[nodiscard]] static Result<RbsbExpertOverride> parse(std::string_view endpoint,
                                                          std::optional<NativePath> ca_bundle);
};

// Capabilities: server-browser.+25.
// The edge both the browser and publish::HostPublisher dial, over UDP with ALPN rbsb/1. Every
// A/AAAA record of `host` is tried in turn; `host` stays the TLS server name.
struct RbsbEndpoint {
    std::string host;
    Port port = kCompiledRbsbPort;
    std::optional<NativePath> ca_bundle;
    EndpointSource source = EndpointSource::Compiled;

    bool operator==(const RbsbEndpoint&) const = default;

    // The HTTPS listener on TCP 443 of the same host, used to explain a failed QUIC connect.
    [[nodiscard]] std::string status_url() const;
};

// The one selection both connections use (rbsb-production-endpoint §1): the signed manifest's
// endpoint, else the expert override, else sb.rebootfn.org:443.
[[nodiscard]] RbsbEndpoint select_rbsb_endpoint(const std::optional<components::EndpointOverride>& manifest,
                                                const std::optional<RbsbExpertOverride>& expert);

}  // namespace rb::browser
