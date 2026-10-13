#pragma once

#include "reboot/api/decode_error.hpp"
#include "reboot/base/diag.hpp"
#include "reboot/base/types.hpp"

namespace rb::api {

// `method_id` travels as the "method" arg.
[[nodiscard]] ::rb::Diagnostic to_diagnostic(DecodeError error, u32 method_id);

}  // namespace rb::api
