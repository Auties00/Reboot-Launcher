#include "firewall_output.hpp"

#include <cstddef>
#include <string>

namespace rb::os_macos::platform {

namespace {

[[nodiscard]] std::string lowered(std::string_view text) {
    std::string out(text);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

[[nodiscard]] bool contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

// "disabled" contains "enabled", so it is checked first.
[[nodiscard]] std::optional<bool> enabled_word(std::string_view lower) {
    if (contains(lower, "disabled")) return false;
    if (contains(lower, "enabled")) return true;
    return std::nullopt;
}

}  // namespace

std::optional<bool> firewall_enabled(std::string_view output) {
    const std::string lower = lowered(output);
    if (contains(lower, "state = 0")) return false;
    if (contains(lower, "state = 1") || contains(lower, "state = 2")) return true;
    return enabled_word(lower);
}

std::optional<bool> firewall_blocks_all(std::string_view output) { return enabled_word(lowered(output)); }

bool firewall_blocks_app(std::string_view output) {
    const std::string lower = lowered(output);
    return contains(lower, "is blocked") && !contains(lower, "not blocked");
}

}  // namespace rb::os_macos::platform
