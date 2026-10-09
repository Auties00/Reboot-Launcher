#pragma once

#include <chrono>
#include <expected>
#include <string>

#include "reboot/contracts/winhost.hpp"
#include "reboot/os_windows/win32session/inject_error.hpp"
#include "unique_handle.hpp"
#include "win32.hpp"

namespace rb::os_windows::win32session {

// The deny-write handle plus the game-side allocation holding the UTF-16 path. The handle pins the
// file against a user-mode swap for the session's lifetime; `remote_path` is the only remote
// memory the primitive leaves live after it returns, and it is reclaimed once the load is
// confirmed.
struct InjectedDll {
    UniqueHandle file;
    contracts::winhost::Bytes path_utf16;  // for the Injected event
    std::wstring base_name;                // for module-enumeration confirmation
    void* remote_path = nullptr;
    std::size_t remote_size = 0;
};

// Opens the DLL deny-write, hashes it through that handle and checks it against `spec.sha256`
// (ERROR_INVALID_IMAGE_HASH on a mismatch), then writes its full UTF-16 path (with terminator)
// into `process`. It never records a load as successful on its own; the caller confirms a queued
// APC through module enumeration, while a straight remote-thread load is waited on and its exit
// code checked here.
class RemoteInjector {
public:
    explicit RemoteInjector(HANDLE process) noexcept : process_(process) {}

    // Opens and integrity-checks the file, allocates and writes the remote path. `thread` is the
    // game's suspended main thread; the load runs when it resumes.
    [[nodiscard]] std::expected<InjectedDll, InjectError> queue_early(const contracts::winhost::InjectSpec& spec,
                                                                      HANDLE thread);

    // Opens and integrity-checks the file, then loads it with a remote thread waited for at most
    // `timeout` and exit-code-checked. Once the file passed its hash, `out` holds its deny-write
    // handle even on failure: after a timeout the load may still happen, so the caller keeps the
    // pin, and the path buffer is left to the target, whose thread may still read it.
    [[nodiscard]] std::expected<void, InjectError> load_now(const contracts::winhost::InjectSpec& spec,
                                                            std::chrono::milliseconds timeout, InjectedDll& out);

private:
    // Shared prelude: OpenFile, Integrity, Allocate, Write. Leaves `out.remote_path` allocated.
    [[nodiscard]] std::expected<void, InjectError> prepare(const contracts::winhost::InjectSpec& spec,
                                                           InjectedDll& out);

    HANDLE process_;
};

// A wait in milliseconds for the Win32 wait functions, never INFINITE.
[[nodiscard]] DWORD wait_millis(std::chrono::milliseconds timeout) noexcept;

// True when `base_name` (a DLL file name) is in `process`'s loaded module list.
[[nodiscard]] bool module_loaded(HANDLE process, const std::wstring& base_name);

}  // namespace rb::os_windows::win32session
