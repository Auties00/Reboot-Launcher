#pragma once

#include <string_view>
#include <type_traits>
#include <vector>

#include "call_failure.hpp"
#include "reboot/client.h"
#include "reboot/contracts/common.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::client {

// Per thread; cleared on entry by every export but rb_abi_version, rb_status_name and the two below.
void clear_last_error() noexcept;
void set_last_error(const contracts::common::WireDiagnostic& diagnostic);
// The encoded WireDiagnostic, empty when the last call succeeded without a warning.
[[nodiscard]] const std::vector<u8>& last_error() noexcept;

// Records the failure and returns its status.
[[nodiscard]] rb_status fail(const CallFailure& failure);
[[nodiscard]] rb_status fail(const Diagnostic& diagnostic);

// Wraps every export body: nothing escapes, and an exception becomes internal.bug.
template <class Body>
auto guarded(std::string_view where, Body&& body) noexcept -> decltype(body()) {
    try {
        clear_last_error();
        return body();
    } catch (...) {
        try {
            set_last_error(contracts::common::to_wire(internal_bug(where)));
        } catch (...) {
        }
        if constexpr (std::is_same_v<decltype(body()), rb_status>) return RB_E_INTERNAL;
    }
}

// client.invalid_argument{name} when `out` is null or still holds data.
[[nodiscard]] Result<void> check_output(const rb_buffer* out, std::string_view name);
// The vector's storage becomes the buffer's, which rb_buffer_release wipes before freeing.
void fill_output(rb_buffer& out, std::vector<u8> bytes);
void wipe_and_free(rb_buffer& buffer) noexcept;

}  // namespace reboot::client
