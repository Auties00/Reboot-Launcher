#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/types.hpp"
#include "reboot/ports/net.hpp"

namespace reboot::net {

// Case-insensitive; the first match, or nullptr.
[[nodiscard]] const std::string* find_header(std::span<const ports::HttpHeader> headers, std::string_view name);

struct HttpResponse {
    u32 status = 0;
    std::vector<ports::HttpHeader> headers;
    std::vector<u8> body;

    [[nodiscard]] const std::string* header(std::string_view name) const { return find_header(headers, name); }
};

}  // namespace reboot::net
