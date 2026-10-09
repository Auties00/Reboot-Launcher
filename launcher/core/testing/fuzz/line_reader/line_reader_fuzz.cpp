#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "reboot/foundation/text.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/process/line_reader.hpp"
#include "reboot/testing/fuzz.hpp"

using rb::testing::fuzz_require;

// Child stderr in chunks sized by the first byte: lines carry no terminator, stay within the cap
// and are valid UTF-8.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const rb::u8> input(data, size);
    rb::process::LineReader reader([](std::string_view line, bool) {
        fuzz_require(line.find_first_of("\r\n") == std::string_view::npos, "a line kept its terminator");
        fuzz_require(line.size() <= rb::process::LineReader::kMaxLineBytes, "a line exceeds the cap");
        fuzz_require(rb::is_valid_utf8(line), "a line is not valid UTF-8");
    });
    const std::size_t chunk = input.empty() ? 1 : std::size_t{input[0]} % 13 + 1;
    for (std::size_t at = 0; at < input.size(); at += chunk)
        reader.feed(input.subspan(at, chunk < input.size() - at ? chunk : input.size() - at));
    reader.finish();
    return 0;
}
