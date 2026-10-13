#pragma once

#include "reboot/base/types.hpp"

namespace rb::api {

// Plain data, so clients need no message registry; the engine's api_dispatch turns it into a Diagnostic.
enum class DecodeError : u8 {
    // Not protobuf wire data for the message.
    Malformed,
    // A oneof with more than one member set; protoc-built peers would keep only the last.
    ConflictingCases,
    // A oneof with no member set: a case added by a newer schema. Events holding one are skipped.
    UnknownCase,
};

}  // namespace rb::api
