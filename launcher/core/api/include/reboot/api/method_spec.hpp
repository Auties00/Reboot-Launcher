#pragma once

#include <string_view>

#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::api {

enum class MethodKind : u8 { Call, Operation };

// What an operation's Progress.done and total count.
enum class ProgressUnit : u8 { NoProgress, ItemCount, ByteCount };

// One row of the generated MethodTable.
struct MethodSpec {
    // service << 16 | method; stable for the life of the major version.
    u32 id = 0;
    std::string_view service;
    std::string_view name;
    MethodKind kind{};
    // Meaningful for operations only.
    DisconnectPolicy default_disconnect{};
    ProgressUnit progress{};
    std::string_view request_type;
    std::string_view response_type;
};

}  // namespace rb::api
