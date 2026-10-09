#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <boost/json/value.hpp>

#include "reboot/foundation/types.hpp"

namespace rb::storage {

// Two-space indents and a final newline, so hand edits and diffs stay readable.
[[nodiscard]] std::string to_pretty_json(const boost::json::value& value);

// Never throws; nullopt for anything that is not exactly one JSON value.
[[nodiscard]] std::optional<boost::json::value> parse_json(std::string_view text);

[[nodiscard]] std::string base64_encode(std::span<const u8> bytes);
[[nodiscard]] std::optional<std::vector<u8>> base64_decode(std::string_view text);

[[nodiscard]] inline std::span<const u8> as_bytes(std::string_view text) noexcept {
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}
[[nodiscard]] inline std::string_view as_text(std::span<const u8> bytes) noexcept {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

}  // namespace rb::storage
