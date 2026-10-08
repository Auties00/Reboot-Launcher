#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "file_park.hpp"

#include <utility>

namespace reboot::os_windows::win32session {

FilePark::FilePark(const std::vector<Bytes>& paths_utf16) {
    for (const Bytes& path_utf16 : paths_utf16) {
        std::wstring original = to_wide(path_utf16);
        if (original.empty() || GetFileAttributesW(original.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        std::wstring parked = original + L".reboot-parked";
        DeleteFileW(parked.c_str());  // a leftover from a crashed session must not block the rename
        if (MoveFileW(original.c_str(), parked.c_str()) == 0) continue;
        parked_.emplace_back(std::move(original), std::move(parked));
    }
}

FilePark::~FilePark() {
    for (const auto& [original, parked] : parked_) MoveFileW(parked.c_str(), original.c_str());
}

}  // namespace reboot::os_windows::win32session
