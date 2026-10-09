#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "file_park.hpp"

#include <utility>

namespace reboot::os_windows::win32session {

namespace {

bool exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

}  // namespace

FilePark::FilePark(const std::vector<Bytes>& paths_utf16) {
    for (const Bytes& path_utf16 : paths_utf16) {
        std::wstring original = to_wide(path_utf16);
        if (original.empty()) continue;
        std::wstring parked = original + kParkedSuffix;
        if (!exists(original)) {
            // A crashed session left only the parked copy: keep it parked and restore it at the end.
            if (exists(parked)) parked_.emplace_back(std::move(original), std::move(parked));
            continue;
        }
        DeleteFileW(parked.c_str());  // a leftover beside a live original must not block the rename
        if (MoveFileExW(original.c_str(), parked.c_str(), 0) == 0) continue;
        parked_.emplace_back(std::move(original), std::move(parked));
    }
}

FilePark::~FilePark() {
    // The parked copy is the user's file, so it wins over anything recreated during the session.
    for (const auto& [original, parked] : parked_)
        MoveFileExW(parked.c_str(), original.c_str(), MOVEFILE_REPLACE_EXISTING);
}

}  // namespace reboot::os_windows::win32session
