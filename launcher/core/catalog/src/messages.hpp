#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::catalog::msg {

REBOOT_MESSAGE_DECL(kFetchFailed);
REBOOT_MESSAGE_DECL(kHttpStatus);
REBOOT_MESSAGE_DECL(kUntrusted);
REBOOT_MESSAGE_DECL(kUnknownSchema);
REBOOT_MESSAGE_DECL(kMalformed);
REBOOT_MESSAGE_DECL(kCacheMissing);
REBOOT_MESSAGE_DECL(kCacheWriteFailed);
REBOOT_MESSAGE_DECL(kBundledUnusable);
REBOOT_MESSAGE_DECL(kEntryNotFound);
REBOOT_MESSAGE_DECL(kEntryNotInstallable);

}  // namespace rb::catalog::msg
