#include <cstddef>
#include <cstdint>
#include <span>

#include "reboot/builds/memory_byte_source.hpp"
#include "reboot/builds/pe_version_reader.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/testing/fuzz.hpp"

using reboot::testing::fuzz_require;

// Untrusted shipping exes: every path through the reader stays inside the input and the cap.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const reboot::u8> input(data, size);
    const reboot::builds::PeVersionReader reader;
    reboot::builds::MemoryByteSource source(input);
    if (const auto resource = reader.read_version_resource(source))
        fuzz_require(resource->size() <= reboot::builds::kVersionResourceCap, "version resource exceeds the cap");
    (void)reader.read_marker(source);
    (void)reader.scan_for_marker(source, reboot::CancelToken{});
    return 0;
}
