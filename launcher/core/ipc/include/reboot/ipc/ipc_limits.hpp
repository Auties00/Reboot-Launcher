#pragma once

#include <chrono>
#include <cstddef>

#include "reboot/foundation/types.hpp"

// The per-connection caps (subscriptions, calls, outbound budget) live in contracts/ipc.hpp.
namespace reboot::ipc {

inline constexpr std::chrono::seconds kHelloDeadline{10};

// Events the engine may have in flight per subscription; the window starts full at Subscribe and
// the client returns Credit for every event its application takes.
inline constexpr u32 kEventCreditWindow = 256;
inline constexpr std::size_t kMaxEventsPerBatch = 64;

// Byte budget of each engine-side Subscription queue.
inline constexpr std::size_t kSubscriptionQueueBytes = std::size_t{4} << 20;

inline constexpr std::chrono::milliseconds kReconnectBackoffMin{250};
inline constexpr std::chrono::milliseconds kReconnectBackoffMax{5000};

// How often a waiting client re-checks state/update-in-progress and retries connect.
inline constexpr std::chrono::milliseconds kConnectPollInterval{200};

// A state/update-in-progress older than this is left from a failed update and no longer holds
// clients back from starting an engine.
inline constexpr std::chrono::seconds kUpdateMarkerTimeout{120};

}  // namespace reboot::ipc
