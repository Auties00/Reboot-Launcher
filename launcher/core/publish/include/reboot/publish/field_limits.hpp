#pragma once

#include <cstddef>

#include "reboot/foundation/types.hpp"

namespace rb::publish {

// rbsb/1 field limits (the edge's FieldLimits), checked before anything is sent so a bad value
// fails the call instead of reaching the edge as BAD_REQUEST. Text limits are UTF-8 bytes;
// GameVersion::canonical() already fits the 16-byte version.
inline constexpr std::size_t kMaxServerNameBytes = 64;
inline constexpr std::size_t kMaxDescriptionBytes = 256;
inline constexpr std::size_t kMaxAuthorBytes = 32;
inline constexpr std::size_t kMaxPasswordBytes = 128;
// Bounds both max_players and the live player count.
inline constexpr u32 kMaxPlayerLimit = 1000;

}  // namespace rb::publish
