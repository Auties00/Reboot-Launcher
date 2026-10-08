#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/net/http_response.hpp"
#include "reboot/ports/net.hpp"

namespace reboot::net {

// A response that carries credentials (tokens, exchange codes); the body is wiped on destruction.
struct SecretHttpResponse {
    u32 status = 0;
    std::vector<ports::HttpHeader> headers;
    SecretBytes body;

    [[nodiscard]] const std::string* header(std::string_view name) const { return find_header(headers, name); }
};

}  // namespace reboot::net
