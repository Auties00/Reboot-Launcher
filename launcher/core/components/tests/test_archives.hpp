#pragma once

#include <string>
#include <utility>
#include <vector>

namespace rb::components::test {

// A gzip'd tar of `files` (path, content), built in memory. Kept in its own translation unit
// because archive.h brings in windows.h and its min/max macros.
[[nodiscard]] std::string make_tar_gz(const std::vector<std::pair<std::string, std::string>>& files);

}  // namespace rb::components::test
