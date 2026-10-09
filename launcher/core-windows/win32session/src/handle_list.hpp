#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

#include "win32.hpp"

namespace reboot::os_windows::win32session {

// Moves the distinct handles to the front and returns how many there are. CreateProcessW rejects a
// PROC_THREAD_ATTRIBUTE_HANDLE_LIST that names a handle twice, and a companion's three stdio
// handles are all the one NUL handle.
template <std::size_t N>
[[nodiscard]] std::size_t unique_handles(std::array<HANDLE, N>& handles) noexcept {
    std::size_t count = 0;
    for (std::size_t i = 0; i < N; ++i)
        if (std::find(handles.begin(), handles.begin() + count, handles[i]) == handles.begin() + count)
            handles[count++] = handles[i];
    return count;
}

}  // namespace reboot::os_windows::win32session
