#pragma once

#include <string>
#include <utility>
#include <vector>

#include "wide.hpp"

namespace reboot::os_windows::win32session {

// Renames each file to <name>.reboot-parked for one session and renames it back on destruction.
// A missing file or a failed rename is skipped rather than failing the launch.
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

}  // namespace reboot::os_windows::win32session
