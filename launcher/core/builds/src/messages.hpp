#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::builds::msg {

REBOOT_MESSAGE_DECL(kNameEmpty);
REBOOT_MESSAGE_DECL(kNameTaken);
REBOOT_MESSAGE_DECL(kPathNotAbsolute);
REBOOT_MESSAGE_DECL(kPathMissing);
REBOOT_MESSAGE_DECL(kNotADirectory);
REBOOT_MESSAGE_DECL(kAlreadyRegistered);
REBOOT_MESSAGE_DECL(kOverlapsBuild);
REBOOT_MESSAGE_DECL(kInsideInstallDir);
REBOOT_MESSAGE_DECL(kNotFound);
REBOOT_MESSAGE_DECL(kInUse);
REBOOT_MESSAGE_DECL(kDestinationNotEmpty);
REBOOT_MESSAGE_DECL(kDestinationBusy);
REBOOT_MESSAGE_DECL(kVolumeReadOnly);
REBOOT_MESSAGE_DECL(kVolumeNetwork);
REBOOT_MESSAGE_DECL(kVolumeRemovable);
REBOOT_MESSAGE_DECL(kVolumeFat);
REBOOT_MESSAGE_DECL(kInsufficientSpace);
REBOOT_MESSAGE_DECL(kBuildUnavailable);
REBOOT_MESSAGE_DECL(kDownloadFailed);
REBOOT_MESSAGE_DECL(kChecksumMismatch);
REBOOT_MESSAGE_DECL(kUnsupportedArchive);
REBOOT_MESSAGE_DECL(kCorruptArchive);
REBOOT_MESSAGE_DECL(kUnsafeEntryPath);
REBOOT_MESSAGE_DECL(kMissingShipping);
REBOOT_MESSAGE_DECL(kMultipleShipping);
REBOOT_MESSAGE_DECL(kShippingNotFound);
REBOOT_MESSAGE_DECL(kUnsupportedVersion);
REBOOT_MESSAGE_DECL(kVersionMismatch);
REBOOT_MESSAGE_DECL(kUnknownInstallFolder);
REBOOT_MESSAGE_DECL(kRemoveFailed);
REBOOT_MESSAGE_DECL(kIo);
REBOOT_MESSAGE_DECL(kCancelled);

REBOOT_MESSAGE_DECL(kPeNotPe);
REBOOT_MESSAGE_DECL(kPeMalformed);
REBOOT_MESSAGE_DECL(kPeNoVersionResource);
REBOOT_MESSAGE_DECL(kPeResourceTooLarge);
REBOOT_MESSAGE_DECL(kPeReadFailed);
REBOOT_MESSAGE_DECL(kVersionFileUnreadable);

REBOOT_MESSAGE_DECL(kNoReleaseMarker);
REBOOT_MESSAGE_DECL(kUnknownReleaseShape);
REBOOT_MESSAGE_DECL(kUnknownChangelist);
REBOOT_MESSAGE_DECL(kCatalogVersionMismatch);
REBOOT_MESSAGE_DECL(kWalkIncomplete);
REBOOT_MESSAGE_DECL(kCaseCollision);

}  // namespace rb::builds::msg
