#include "reboot/storage/console_key.hpp"

#include <algorithm>
#include <array>

#include "messages.hpp"

namespace reboot::storage {

namespace {

// Unreal Engine FKey names (EKeys) for the keys a keyboard console binding can use.
constexpr auto kUnrealKeyNames = std::to_array<std::string_view>({
    "Tilde",        "F1",           "F2",           "F3",          "F4",          "F5",          "F6",
    "F7",           "F8",           "F9",           "F10",         "F11",         "F12",         "A",
    "B",            "C",            "D",            "E",           "F",           "G",           "H",
    "I",            "J",            "K",            "L",           "M",           "N",           "O",
    "P",            "Q",            "R",            "S",           "T",           "U",           "V",
    "W",            "X",            "Y",            "Z",           "Zero",        "One",         "Two",
    "Three",        "Four",         "Five",         "Six",         "Seven",       "Eight",       "Nine",
    "Enter",        "Escape",       "BackSpace",    "Tab",         "SpaceBar",    "Hyphen",      "Equals",
    "LeftBracket",  "RightBracket", "Backslash",    "Semicolon",   "Apostrophe",  "Comma",       "Period",
    "Slash",        "CapsLock",     "ScrollLock",   "Pause",       "Insert",      "Home",        "PageUp",
    "Delete",       "End",          "PageDown",     "NumLock",     "Divide",      "Multiply",    "Subtract",
    "Add",          "Decimal",      "NumPadZero",   "NumPadOne",   "NumPadTwo",   "NumPadThree", "NumPadFour",
    "NumPadFive",   "NumPadSix",    "NumPadSeven",  "NumPadEight", "NumPadNine",  "Up",          "Down",
    "Left",         "Right",        "LeftShift",    "RightShift",  "LeftControl", "RightControl", "LeftAlt",
    "RightAlt",     "LeftCommand",  "RightCommand"});

}  // namespace

Result<ConsoleKey> ConsoleKey::parse(std::string_view name) {
    if (std::ranges::find(kUnrealKeyNames, name) == kUnrealKeyNames.end())
        return invalid_input(msg::kInvalidConsoleKey).arg("key", name).fail();
    return ConsoleKey{std::string(name)};
}

std::span<const std::string_view> unreal_key_names() noexcept { return kUnrealKeyNames; }

}  // namespace reboot::storage
