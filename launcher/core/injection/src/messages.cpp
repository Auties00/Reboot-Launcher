#include "messages.hpp"

namespace reboot::injection::msg {

REBOOT_MESSAGE(kDllPathEmpty, "injection.dll_path_empty", "Choose a DLL file");
REBOOT_MESSAGE(kDllMissing, "injection.dll_missing", "{path} does not exist");
REBOOT_MESSAGE(kDllNotDll, "injection.dll_not_dll", "{path} is not a DLL");
REBOOT_MESSAGE(kDllNotPe64, "injection.dll_not_pe64", "{path} is not a 64-bit Windows DLL");
REBOOT_MESSAGE(kDllUnreadable, "injection.dll_unreadable", "{path} cannot be read");
REBOOT_MESSAGE(kInjectFailed, "injection.inject_failed", "{path} could not be loaded into the game");

}  // namespace reboot::injection::msg
