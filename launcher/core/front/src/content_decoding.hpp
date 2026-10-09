#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "reboot/foundation/types.hpp"

namespace rb::front {

// A response body as the upstream meant it, for learning only: identity, gzip or deflate. nullopt for
// another coding, corrupt data, or more than `cap` decoded bytes.
[[nodiscard]] std::optional<std::vector<u8>> decode_content(std::string_view content_encoding,
                                                            std::span<const u8> body, std::size_t cap);

}  // namespace rb::front
