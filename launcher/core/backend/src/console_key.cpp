#include "reboot/backend/console_key.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace rb::backend {

namespace {

struct ConsoleKeyRow {
    u16 usage;
    std::string_view name;
    std::string_view label;
};

// One row per name in storage's unreal_key_names().
constexpr auto kConsoleKeys = std::to_array<ConsoleKeyRow>({
    {0x35, "Tilde", "`"},
    {0x3A, "F1", "F1"},
    {0x3B, "F2", "F2"},
    {0x3C, "F3", "F3"},
    {0x3D, "F4", "F4"},
    {0x3E, "F5", "F5"},
    {0x3F, "F6", "F6"},
    {0x40, "F7", "F7"},
    {0x41, "F8", "F8"},
    {0x42, "F9", "F9"},
    {0x43, "F10", "F10"},
    {0x44, "F11", "F11"},
    {0x45, "F12", "F12"},
    {0x04, "A", "A"},
    {0x05, "B", "B"},
    {0x06, "C", "C"},
    {0x07, "D", "D"},
    {0x08, "E", "E"},
    {0x09, "F", "F"},
    {0x0A, "G", "G"},
    {0x0B, "H", "H"},
    {0x0C, "I", "I"},
    {0x0D, "J", "J"},
    {0x0E, "K", "K"},
    {0x0F, "L", "L"},
    {0x10, "M", "M"},
    {0x11, "N", "N"},
    {0x12, "O", "O"},
    {0x13, "P", "P"},
    {0x14, "Q", "Q"},
    {0x15, "R", "R"},
    {0x16, "S", "S"},
    {0x17, "T", "T"},
    {0x18, "U", "U"},
    {0x19, "V", "V"},
    {0x1A, "W", "W"},
    {0x1B, "X", "X"},
    {0x1C, "Y", "Y"},
    {0x1D, "Z", "Z"},
    {0x27, "Zero", "0"},
    {0x1E, "One", "1"},
    {0x1F, "Two", "2"},
    {0x20, "Three", "3"},
    {0x21, "Four", "4"},
    {0x22, "Five", "5"},
    {0x23, "Six", "6"},
    {0x24, "Seven", "7"},
    {0x25, "Eight", "8"},
    {0x26, "Nine", "9"},
    {0x28, "Enter", "Enter"},
    {0x29, "Escape", "Esc"},
    {0x2A, "BackSpace", "Backspace"},
    {0x2B, "Tab", "Tab"},
    {0x2C, "SpaceBar", "Space"},
    {0x2D, "Hyphen", "-"},
    {0x2E, "Equals", "="},
    {0x2F, "LeftBracket", "["},
    {0x30, "RightBracket", "]"},
    {0x31, "Backslash", "\\"},
    {0x33, "Semicolon", ";"},
    {0x34, "Apostrophe", "'"},
    {0x36, "Comma", ","},
    {0x37, "Period", "."},
    {0x38, "Slash", "/"},
    {0x39, "CapsLock", "Caps Lock"},
    {0x47, "ScrollLock", "Scroll Lock"},
    {0x48, "Pause", "Pause"},
    {0x49, "Insert", "Insert"},
    {0x4A, "Home", "Home"},
    {0x4B, "PageUp", "Page Up"},
    {0x4C, "Delete", "Delete"},
    {0x4D, "End", "End"},
    {0x4E, "PageDown", "Page Down"},
    {0x53, "NumLock", "Num Lock"},
    {0x54, "Divide", "Num /"},
    {0x55, "Multiply", "Num *"},
    {0x56, "Subtract", "Num -"},
    {0x57, "Add", "Num +"},
    {0x63, "Decimal", "Num ."},
    {0x62, "NumPadZero", "Num 0"},
    {0x59, "NumPadOne", "Num 1"},
    {0x5A, "NumPadTwo", "Num 2"},
    {0x5B, "NumPadThree", "Num 3"},
    {0x5C, "NumPadFour", "Num 4"},
    {0x5D, "NumPadFive", "Num 5"},
    {0x5E, "NumPadSix", "Num 6"},
    {0x5F, "NumPadSeven", "Num 7"},
    {0x60, "NumPadEight", "Num 8"},
    {0x61, "NumPadNine", "Num 9"},
    {0x52, "Up", "Up"},
    {0x51, "Down", "Down"},
    {0x50, "Left", "Left"},
    {0x4F, "Right", "Right"},
    {0xE1, "LeftShift", "Left Shift"},
    {0xE5, "RightShift", "Right Shift"},
    {0xE0, "LeftControl", "Left Ctrl"},
    {0xE4, "RightControl", "Right Ctrl"},
    {0xE2, "LeftAlt", "Left Alt"},
    {0xE6, "RightAlt", "Right Alt"},
    {0xE3, "LeftCommand", "Left Command"},
    {0xE7, "RightCommand", "Right Command"},
});

constexpr u16 kHidKeypadEnter = 0x58;
constexpr u16 kHidIsoBackslash = 0x64;
constexpr u16 kHidEnter = 0x28;
constexpr u16 kHidBackslash = 0x31;

}  // namespace

std::optional<ConsoleKey> console_key_from_hid(HidUsage usage) {
    u16 id = usage.id;
    if (id == kHidKeypadEnter) id = kHidEnter;
    if (id == kHidIsoBackslash) id = kHidBackslash;
    const auto it = std::ranges::find(kConsoleKeys, id, &ConsoleKeyRow::usage);
    if (it == kConsoleKeys.end()) return std::nullopt;
    return ConsoleKey{std::string(it->name)};
}

std::string_view key_label(const ConsoleKey& key) noexcept {
    const auto it = std::ranges::find(kConsoleKeys, std::string_view(key.name), &ConsoleKeyRow::name);
    return it == kConsoleKeys.end() ? std::string_view{} : it->label;
}

}  // namespace rb::backend
