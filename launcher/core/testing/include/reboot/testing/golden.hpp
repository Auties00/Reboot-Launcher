#pragma once

#include <span>
#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::testing {

// Golden vectors live under <package>/tests/data/ and are compared byte for byte.
[[nodiscard]] Result<std::vector<u8>> read_golden(const NativePath& file);

// Empty when equal; otherwise the first differing offset, both lengths and a hex window around it.
[[nodiscard]] std::string golden_mismatch(std::span<const u8> expected, std::span<const u8> actual);

// Compares the whole encoded frame (type, length and payload) of `message` against `file`.
template <ContractMessage T>
[[nodiscard]] std::string contract_golden_mismatch(const NativePath& file, const T& message) {
    auto expected = read_golden(file);
    if (!expected) return "cannot read " + display_utf8(file);
    return golden_mismatch(*expected, encode_contract_frame(message));
}

}  // namespace reboot::testing
