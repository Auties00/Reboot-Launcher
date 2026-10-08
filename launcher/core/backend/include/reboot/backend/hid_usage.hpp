#pragma once

#include <compare>
#include <optional>

#include "reboot/foundation/types.hpp"

namespace reboot::backend {

// A usage id on the USB HID Keyboard/Keypad page (0x07): a physical key position, whatever the
// layout. UIs capture keys natively and convert here, so the tables exist once.
struct HidUsage {
    u16 id = 0;

    constexpr auto operator<=>(const HidUsage&) const = default;
};

// Set 1 scancode as WM_KEYDOWN and WinUI KeyStatus report it, with the 0xE0 prefix as `extended`.
// 0x45 is Pause without `extended` and Num Lock with it.
[[nodiscard]] std::optional<HidUsage> hid_from_windows_scancode(u16 scancode, bool extended) noexcept;
// NSEvent.keyCode, the kVK_* virtual key codes.
[[nodiscard]] std::optional<HidUsage> hid_from_macos_keycode(u16 keycode) noexcept;
// A linux/input-event-codes.h KEY_* code: the X11 or xkb keycode minus 8.
[[nodiscard]] std::optional<HidUsage> hid_from_evdev(u16 code) noexcept;

}  // namespace reboot::backend
