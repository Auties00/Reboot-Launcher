#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>

#include "reboot/backend/console_key.hpp"
#include "reboot/backend/hid_usage.hpp"

using namespace rb;
using namespace rb::backend;

TEST_CASE("every allowlisted key name has a label", "[backend]") {
    for (const std::string_view name : storage::unreal_key_names())
        CHECK_FALSE(key_label(ConsoleKey{std::string(name)}).empty());
    CHECK(key_label(ConsoleKey{"NotAKey"}).empty());
}

TEST_CASE("every console key a HID usage maps to is allowlisted", "[backend]") {
    for (u16 id = 0; id <= 0xFF; ++id) {
        const std::optional<ConsoleKey> key = console_key_from_hid(HidUsage{id});
        if (key) CHECK(ConsoleKey::parse(key->name));
    }
}

TEST_CASE("digit zero is labelled 0", "[backend]") {
    const std::optional<ConsoleKey> zero = console_key_from_hid(HidUsage{0x27});
    REQUIRE(zero);
    CHECK(zero->name == "Zero");
    CHECK(key_label(*zero) == "0");
}

TEST_CASE("keys UE does not tell apart share a name", "[backend]") {
    CHECK(console_key_from_hid(HidUsage{0x58}) == console_key_from_hid(HidUsage{0x28}));
    CHECK(console_key_from_hid(HidUsage{0x64}) == console_key_from_hid(HidUsage{0x31}));
    CHECK_FALSE(console_key_from_hid(HidUsage{0x46}));
}

TEST_CASE("the three platforms agree on the same physical keys", "[backend]") {
    constexpr HidUsage kF8{0x41};
    CHECK(hid_from_windows_scancode(0x42, false) == kF8);
    CHECK(hid_from_macos_keycode(0x64) == kF8);
    CHECK(hid_from_evdev(66) == kF8);

    constexpr HidUsage kGrave{0x35};
    CHECK(hid_from_windows_scancode(0x29, false) == kGrave);
    CHECK(hid_from_macos_keycode(0x32) == kGrave);
    CHECK(hid_from_evdev(41) == kGrave);

    constexpr HidUsage kRightControl{0xE4};
    CHECK(hid_from_windows_scancode(0x1D, true) == kRightControl);
    CHECK(hid_from_macos_keycode(0x3E) == kRightControl);
    CHECK(hid_from_evdev(97) == kRightControl);
}

TEST_CASE("Windows reports Pause and Num Lock with the same scancode", "[backend]") {
    CHECK(hid_from_windows_scancode(0x45, false) == HidUsage{0x48});
    CHECK(hid_from_windows_scancode(0x45, true) == HidUsage{0x53});
    CHECK(hid_from_evdev(69) == HidUsage{0x53});
    CHECK(hid_from_evdev(119) == HidUsage{0x48});
}

TEST_CASE("unknown codes map to nothing", "[backend]") {
    CHECK_FALSE(hid_from_windows_scancode(0x00, false));
    CHECK_FALSE(hid_from_windows_scancode(0x7F, true));
    CHECK_FALSE(hid_from_macos_keycode(0xFF));
    CHECK_FALSE(hid_from_evdev(0));
    CHECK_FALSE(hid_from_evdev(500));
}
