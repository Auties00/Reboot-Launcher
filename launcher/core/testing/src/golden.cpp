#include "reboot/testing/golden.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace rb::testing {
namespace {

constexpr std::size_t kWindow = 16;

[[nodiscard]] std::string hex_window(std::span<const u8> bytes, std::size_t around) {
    const std::size_t from = around > kWindow ? around - kWindow : 0;
    const std::size_t to = std::min(bytes.size(), around + kWindow);
    std::string out;
    for (std::size_t i = from; i < to; ++i)
        out += i == around ? std::format("[{:02x}]", bytes[i]) : std::format(" {:02x}", bytes[i]);
    return out;
}

}  // namespace

Result<std::vector<u8>> read_golden(const NativePath& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return make_diag(kTestingDomain, msg::kNotFound).arg("path", file).kind(ErrorKind::NotFound).fail();
    const std::vector<char> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return std::vector<u8>(bytes.begin(), bytes.end());
}

std::string golden_mismatch(std::span<const u8> expected, std::span<const u8> actual) {
    const auto [at_expected, at_actual] = std::ranges::mismatch(expected, actual);
    if (at_expected == expected.end() && at_actual == actual.end()) return {};
    const auto offset = static_cast<std::size_t>(at_expected - expected.begin());
    return std::format("first difference at byte {} (expected {} bytes, got {})\n  expected:{}\n  actual:  {}", offset,
                       expected.size(), actual.size(), hex_window(expected, offset), hex_window(actual, offset));
}

}  // namespace rb::testing
