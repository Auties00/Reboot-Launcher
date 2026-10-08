#pragma once

#include <string>
#include <string_view>

namespace reboot {

[[nodiscard]] bool is_valid_utf8(std::string_view text);

// NFC; strips bidi overrides and isolates (U+202A-202E, U+2066-2069), U+200B, U+2060, U+FEFF
// and C0/C1 controls except tab; keeps ZWJ, ZWNJ, LRM and RLM.
[[nodiscard]] std::string sanitize_display_text(std::string_view text);

// sanitize_display_text, capped at 2048 bytes on a code point boundary.
[[nodiscard]] std::string normalize_detail(std::string_view text);

// Both replace invalid sequences with U+FFFD.
[[nodiscard]] std::u16string utf8_to_utf16(std::string_view text);
[[nodiscard]] std::string utf16_to_utf8(std::u16string_view text);

[[nodiscard]] bool iequals_ascii(std::string_view a, std::string_view b);

}  // namespace reboot
