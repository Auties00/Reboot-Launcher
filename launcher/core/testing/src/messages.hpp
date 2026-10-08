#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::testing {

// The foundation has no Testing error domain yet; ids keep the stable "testing." prefix.
inline constexpr ErrorDomain kTestingDomain = ErrorDomain::Unknown;

}  // namespace reboot::testing

namespace reboot::testing::msg {

REBOOT_MESSAGE_DECL(kUnscriptedSpawn);
REBOOT_MESSAGE_DECL(kNoHttpRoute);
REBOOT_MESSAGE_DECL(kNotFound);
REBOOT_MESSAGE_DECL(kNotADirectory);
REBOOT_MESSAGE_DECL(kLockHeld);
REBOOT_MESSAGE_DECL(kLockWaitExceeded);
REBOOT_MESSAGE_DECL(kHeldOpen);
REBOOT_MESSAGE_DECL(kNoVolume);
REBOOT_MESSAGE_DECL(kUnresolvedHost);
REBOOT_MESSAGE_DECL(kCancelled);
REBOOT_MESSAGE_DECL(kSecretStoreUnavailable);
REBOOT_MESSAGE_DECL(kHttpsOnly);
REBOOT_MESSAGE_DECL(kEndpointInUse);
REBOOT_MESSAGE_DECL(kNoListener);
REBOOT_MESSAGE_DECL(kNoSuchProcess);
REBOOT_MESSAGE_DECL(kNoRuntimeLayout);
REBOOT_MESSAGE_DECL(kStreamClosed);
REBOOT_MESSAGE_DECL(kBadScript);
REBOOT_MESSAGE_DECL(kMalformedReply);
REBOOT_MESSAGE_DECL(kBadBootstrap);

// Owned and registered by the OS packages; the fakes fail with the same ids the real adapters use.
inline constexpr MessageId kEndpointUntrusted{"ipc.endpoint_untrusted"};
inline constexpr MessageId kNotSupported{"platform.not_supported"};

}  // namespace reboot::testing::msg
