#pragma once

#include <format>
#include <string_view>

namespace sb::log {

enum class Level { debug = 0, info = 1, warn = 2, error = 3 };

void init(Level min, bool json);
[[nodiscard]] Level parse_level(std::string_view s) noexcept;
[[nodiscard]] bool enabled(Level l) noexcept;
void write(Level l, std::string_view msg) noexcept;

// Logging is off the hot paths (startup, lifecycle, errors), so a synchronous writer suffices.
template <class... A>
void debug(std::format_string<A...> f, A&&... a) {
    if (enabled(Level::debug)) write(Level::debug, std::format(f, std::forward<A>(a)...));
}
template <class... A>
void info(std::format_string<A...> f, A&&... a) {
    if (enabled(Level::info)) write(Level::info, std::format(f, std::forward<A>(a)...));
}
template <class... A>
void warn(std::format_string<A...> f, A&&... a) {
    if (enabled(Level::warn)) write(Level::warn, std::format(f, std::forward<A>(a)...));
}
template <class... A>
void error(std::format_string<A...> f, A&&... a) {
    if (enabled(Level::error)) write(Level::error, std::format(f, std::forward<A>(a)...));
}

}  // namespace sb::log
