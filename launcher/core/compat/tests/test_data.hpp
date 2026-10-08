#pragma once

#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::compat::test {

[[nodiscard]] inline NativePath data_path(std::string_view name) { return NativePath(REBOOT_COMPAT_TEST_DATA) / name; }

[[nodiscard]] inline std::string read_text(std::string_view name) {
    std::ifstream in(data_path(name), std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

[[nodiscard]] inline std::vector<u8> read_bytes(std::string_view name) {
    const std::string text = read_text(name);
    return {text.begin(), text.end()};
}

}  // namespace reboot::compat::test
