#pragma once

#include <string_view>
#include <thread>
#include <utility>

#include "reboot/foundation/diag.hpp"
#include "win32.hpp"
#include "win_error.hpp"

namespace reboot::os_windows::platform {

// Runs `body` on a fresh thread inside its own COM apartment and waits for it. Worker threads carry
// no apartment, and joining one here keeps the shell and Task Scheduler off the caller's state.
template <class T, class Body>
[[nodiscard]] Result<T> in_apartment(DWORD apartment, std::string_view where, Body body) {
    Result<T> result = std::unexpected(internal_bug(where));
    try {
        std::thread worker([&] {
            const HRESULT init = CoInitializeEx(nullptr, apartment | COINIT_DISABLE_OLE1DDE);
            if (FAILED(init)) {
                result = std::unexpected(hresult_failed("CoInitializeEx", init));
                return;
            }
            try {
                result = body();
            } catch (...) {
                result = std::unexpected(internal_bug(where));
            }
            CoUninitialize();
        });
        worker.join();
    } catch (...) {
        return std::unexpected(internal_bug(where));
    }
    return result;
}

}  // namespace reboot::os_windows::platform
