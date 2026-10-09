#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::components {

REBOOT_MESSAGE_DECL(kManifestMalformed);
REBOOT_MESSAGE_DECL(kManifestSchemaUnsupported);
REBOOT_MESSAGE_DECL(kManifestExpired);
REBOOT_MESSAGE_DECL(kManifestUnavailable);
REBOOT_MESSAGE_DECL(kManifestFetchFailed);
REBOOT_MESSAGE_DECL(kNoPayload);
REBOOT_MESSAGE_DECL(kPayloadAbiMismatch);
REBOOT_MESSAGE_DECL(kUnknownRuntime);
REBOOT_MESSAGE_DECL(kRuntimeWrongPlatform);
REBOOT_MESSAGE_DECL(kUnknownComponent);
REBOOT_MESSAGE_DECL(kPinned);
REBOOT_MESSAGE_DECL(kDownloadFailed);
REBOOT_MESSAGE_DECL(kChecksumMismatch);
REBOOT_MESSAGE_DECL(kExtractFailed);
REBOOT_MESSAGE_DECL(kStoreFailed);
REBOOT_MESSAGE_DECL(kDownloadQuarantined);
REBOOT_MESSAGE_DECL(kDownloadVanished);
REBOOT_MESSAGE_DECL(kFileQuarantined);
REBOOT_MESSAGE_DECL(kFileVanished);
REBOOT_MESSAGE_DECL(kHelperQuarantined);
REBOOT_MESSAGE_DECL(kHelperVanished);
REBOOT_MESSAGE_DECL(kHeldFileMissing);
REBOOT_MESSAGE_DECL(kHeldFileAccessDenied);
REBOOT_MESSAGE_DECL(kFileInUse);
REBOOT_MESSAGE_DECL(kHeldFileUnreadable);
REBOOT_MESSAGE_DECL(kHeldFileChanged);
REBOOT_MESSAGE_DECL(kBundledAssetMissing);

}  // namespace rb::components
