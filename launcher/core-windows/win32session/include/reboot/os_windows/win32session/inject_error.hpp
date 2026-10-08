#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::os_windows::win32session {

// Where injection of one DLL failed. OpenFile doubles as the accessibility probe that detects an
// antivirus quarantine; RemoteLoad is a remote LoadLibraryW that returned 0.
enum class InjectStep : u8 {
    OpenFile,
    Integrity,
    Allocate,
    Write,
    ResolveLoader,
    QueueApc,
    CreateThread,
    WaitThread,
    RemoteLoad,
    Confirm,
};

// `error` carries the Windows code; its origin is Host when the injector runs in the engine and is
// remapped to GuestWindows by the winhost relay when it runs inside the Wine prefix.
struct InjectError {
    InjectStep step{};
    SystemError error{};
};

}  // namespace reboot::os_windows::win32session
