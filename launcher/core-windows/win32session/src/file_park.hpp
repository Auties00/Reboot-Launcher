#pragma once

#include <string>
#include <utility>
#include <vector>

#include "wide.hpp"

namespace rb::os_windows::win32session {

inline constexpr wchar_t kParkedSuffix[] = L".reboot-parked";

// Renames each file to <name>.reboot-parked for one session and renames it back on destruction.
// A missing file or a failed rename is skipped rather than failing the launch; a parked copy left
// by a crashed session with no original beside it is adopted and restored with the others.
class FilePark {
public:
    explicit FilePark(const std::vector<Bytes>& paths_utf16);
    ~FilePark();
    FilePark(const FilePark&) = delete;
    FilePark& operator=(const FilePark&) = delete;

private:
    // original, parked
    std::vector<std::pair<std::wstring, std::wstring>> parked_;
};

}  // namespace rb::os_windows::win32session
