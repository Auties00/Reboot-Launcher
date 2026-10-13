#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include "reboot/base/diag.hpp"

// The message registry: every id's English template, declared once per id with REBOOT_MESSAGE.
namespace rb {

struct ArgSpec {
    std::string_view name;
};

struct MessageSpec {
    std::string_view id;
    std::string_view english;
    std::span<const ArgSpec> args;
};

// Every message declared with REBOOT_MESSAGE in the linked program; valid after static init.
[[nodiscard]] std::span<const MessageSpec* const> message_registry();

// 0 ok, 1 generic, 2 invalid input, 3 not found, 4 conflict or busy, 5 engine unavailable,
// 6 unsupported, 130 cancelled.
[[nodiscard]] int exit_code_for(const Diagnostic& diag);

[[nodiscard]] Diagnostic internal_bug(std::string_view where);

namespace detail {

[[nodiscard]] constexpr bool is_placeholder_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

// Visits each distinct "{name}" in order of first appearance.
template <class F>
constexpr std::size_t for_each_placeholder(std::string_view text, F&& visit) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '{') continue;
        std::size_t end = i + 1;
        while (end < text.size() && is_placeholder_char(text[end])) ++end;
        if (end == i + 1 || end == text.size() || text[end] != '}') continue;
        const std::string_view name = text.substr(i + 1, end - i - 1);
        if (text.substr(0, i).find(text.substr(i, end - i + 1)) == std::string_view::npos) {
            visit(count, name);
            ++count;
        }
        i = end;
    }
    return count;
}

[[nodiscard]] constexpr std::size_t count_placeholders(std::string_view text) {
    return for_each_placeholder(text, [](std::size_t, std::string_view) {});
}

template <std::size_t N>
[[nodiscard]] constexpr std::array<ArgSpec, N> placeholders(std::string_view text) {
    std::array<ArgSpec, N> out{};
    for_each_placeholder(text, [&](std::size_t index, std::string_view name) { out[index] = ArgSpec{name}; });
    return out;
}

class MessageRegistration {
public:
    explicit MessageRegistration(const MessageSpec& spec) noexcept;

    const MessageSpec* spec;
    const MessageRegistration* next;
};

}  // namespace detail

}  // namespace rb

// Used once per id, at namespace scope in the package's src/messages.cpp.
#define REBOOT_MESSAGE(ident, dotted_id, english)                                                        \
    static constexpr auto ident##_arg_specs_ =                                                           \
        ::rb::detail::placeholders<::rb::detail::count_placeholders(english)>(english);         \
    static constexpr ::rb::MessageSpec ident##_spec_{dotted_id, english, ident##_arg_specs_};      \
    static const ::rb::detail::MessageRegistration ident##_registration_{ident##_spec_};           \
    extern const ::rb::MessageId ident;                                                             \
    const ::rb::MessageId ident { dotted_id }

// Declares an id for the package's other translation units (src/messages.hpp).
#define REBOOT_MESSAGE_DECL(ident) extern const ::rb::MessageId ident
