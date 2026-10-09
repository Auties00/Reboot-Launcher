#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/types.hpp"
#include "reboot/net/resumable_downloader.hpp"
#include "reboot/ports/net.hpp"

namespace reboot::net {

struct ContentRange {
    u64 first = 0;
    u64 last = 0;
    // Absent for "bytes a-b/*".
    std::optional<u64> total;
};

// "bytes a-b/total" or "bytes a-b/*".
[[nodiscard]] std::optional<ContentRange> parse_content_range(std::string_view value);

[[nodiscard]] std::optional<u64> parse_decimal(std::string_view value);

// The strong ETag, else Last-Modified, else empty.
[[nodiscard]] std::string response_validator(const std::vector<ports::HttpHeader>& headers);

// Three lines after a version line; neither the URL nor a validator can hold a line break.
[[nodiscard]] std::vector<u8> encode_sidecar(const ResumeSidecar& sidecar);
[[nodiscard]] std::optional<ResumeSidecar> decode_sidecar(std::span<const u8> bytes);

}  // namespace reboot::net
