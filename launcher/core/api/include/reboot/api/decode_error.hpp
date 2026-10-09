#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::api {

enum class DecodeError : u8 {
    // Not protobuf wire data for the message.
    Malformed,
    // A oneof with more than one member set; protoc-built peers would keep only the last.
    ConflictingCases,
    // A oneof with no member set: a case added by a newer schema. Events holding one are skipped.
    UnknownCase,
};

// `method_id` travels as the "method" arg.
[[nodiscard]] ::rb::Diagnostic to_diagnostic(DecodeError error, u32 method_id);

}  // namespace rb::api
