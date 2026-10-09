#include "wide.hpp"

#include "reboot/foundation/text.hpp"

namespace rb::os_windows::ipc {

std::wstring to_wide(std::string_view utf8) {
    const std::u16string units = utf8_to_utf16(utf8);
    return {units.begin(), units.end()};
}

std::string to_utf8(std::wstring_view wide) {
    const std::u16string units(wide.begin(), wide.end());
    return utf16_to_utf8(units);
}

}  // namespace rb::os_windows::ipc
