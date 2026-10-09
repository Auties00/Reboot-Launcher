#include "language_lookup.hpp"

namespace rb::ux {

std::vector<std::string_view> lookup_fallbacks(std::string_view range) {
    std::vector<std::string_view> out;
    while (!range.empty()) {
        out.push_back(range);
        std::size_t dash = range.rfind('-');
        if (dash == std::string_view::npos) break;
        range = range.substr(0, dash);
        dash = range.rfind('-');
        if (dash != std::string_view::npos && range.size() - dash == 2) range = range.substr(0, dash);
    }
    return out;
}

}  // namespace rb::ux
