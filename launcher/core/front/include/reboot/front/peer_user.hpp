#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::front {

// Engine: the connecting socket has the engine's uid. Unchecked: the OS cannot tell (Windows, macOS).
enum class PeerUser : u8 { Unchecked, Engine, Other };

// Fails closed: Other unless the uid equals `engine_uid`, or the inspector is platform-unsupported.
[[nodiscard]] PeerUser classify_peer(const Result<std::optional<u32>>& peer_uid,
                                     std::optional<u32> engine_uid) noexcept;

}  // namespace reboot::front
