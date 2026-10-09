#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "reboot/browser/deep_link.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/testing/fuzz.hpp"

using rb::testing::fuzz_require;

// Links arrive from the OS URL handler: whatever parses names a server that reparses the same.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    const auto link = rb::browser::parse_deep_link(text);
    if (!link) return 0;
    const std::string canonical =
        std::string(rb::browser::kDeepLinkScheme) + "://" + rb::format_uuid(link->server.value);
    const auto again = rb::browser::parse_deep_link(canonical);
    fuzz_require(again && again->server == link->server, "a parsed link does not round-trip");
    return 0;
}
