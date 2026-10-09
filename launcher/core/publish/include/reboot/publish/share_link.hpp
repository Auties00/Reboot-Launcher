#pragma once

#include <string>
#include <string_view>

#include "reboot/foundation/types.hpp"

namespace rb::publish {

// Lowercase: URL schemes are case-insensitive and Linux handlers register it this way.
inline constexpr std::string_view kShareScheme = "reboot";

// Capabilities: hosting.share.
// reboot://<server_id>. The receiver resolves it through rbsb/1 Resolve, never through an IP, so it
// also works for Unlisted servers.
struct ShareLink {
    ServerId server;

    [[nodiscard]] std::string url() const {
        std::string out(kShareScheme);
        out += "://";
        out += format_uuid(server.value);
        return out;
    }

    bool operator==(const ShareLink&) const = default;
};

}  // namespace rb::publish
