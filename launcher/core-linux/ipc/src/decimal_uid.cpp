#include "decimal_uid.hpp"

#include <charconv>
#include <limits>
#include <system_error>

namespace rb::os_linux::ipc {

std::optional<u32> parse_decimal_uid(std::string_view text) noexcept {
    if (text.empty() || text.front() < '0' || text.front() > '9') return std::nullopt;
    if (text.size() > 1 && text.front() == '0') return std::nullopt;
    u32 uid = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), uid);
    if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    if (uid == std::numeric_limits<u32>::max()) return std::nullopt;
    return uid;
}

}  // namespace rb::os_linux::ipc
