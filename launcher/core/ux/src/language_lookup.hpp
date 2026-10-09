#pragma once

#include <string_view>
#include <vector>

namespace rb::ux {

// RFC 4647 Lookup's truncation of one range: "zh-Hant-TW", "zh-Hant", "zh"; a singleton left last is dropped too.
[[nodiscard]] std::vector<std::string_view> lookup_fallbacks(std::string_view range);

}  // namespace rb::ux
