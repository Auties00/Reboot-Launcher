#include "messages.hpp"

namespace reboot::components {

REBOOT_MESSAGE(kManifestMalformed, "components.manifest_malformed", "The release manifest is malformed at {field}");
REBOOT_MESSAGE(kManifestSchemaUnsupported, "components.manifest_schema_unsupported",
               "The release manifest uses schema {schema}, which this launcher cannot read");
REBOOT_MESSAGE(kManifestExpired, "components.manifest_expired",
               "The downloaded release manifest expired at {expires_at}");
REBOOT_MESSAGE(kManifestUnavailable, "components.manifest_unavailable", "No valid release manifest is available");
REBOOT_MESSAGE(kManifestFetchFailed, "components.manifest_fetch_failed",
               "Cannot download the release manifest from {url}");
REBOOT_MESSAGE(kNoPayload, "components.no_payload",
               "The release manifest has no game payload for payload ABI {payload_abi}");
REBOOT_MESSAGE(kPayloadAbiMismatch, "components.payload_abi_mismatch",
               "Game payload {version} has payload ABI {payload_abi}, but this launcher needs {expected}");
REBOOT_MESSAGE(kUnknownRuntime, "components.unknown_runtime", "The release manifest has no runtime {runtime_id}");
REBOOT_MESSAGE(kRuntimeWrongPlatform, "components.runtime_wrong_platform",
               "Runtime {runtime_id} is not built for this system");
REBOOT_MESSAGE(kUnknownComponent, "components.unknown_component", "There is no component {component}");
REBOOT_MESSAGE(kPinned, "components.pinned", "{component} {version} is in use by a running session");
REBOOT_MESSAGE(kDownloadFailed, "components.download_failed", "Cannot download {component} {version}");
REBOOT_MESSAGE(kChecksumMismatch, "components.checksum_mismatch",
               "The download of {file} does not match its published checksum");
REBOOT_MESSAGE(kExtractFailed, "components.extract_failed", "Cannot unpack {component} {version}");
REBOOT_MESSAGE(kStoreFailed, "components.store_failed", "Cannot write {path} in the component store");

REBOOT_MESSAGE(kDownloadQuarantined, "components.download_quarantined",
               "{file} disappeared or became unreadable right after it was downloaded, likely removed by "
               "{security_products}");
REBOOT_MESSAGE(kDownloadVanished, "components.download_vanished",
               "{file} disappeared or became unreadable right after it was downloaded");
REBOOT_MESSAGE(kFileQuarantined, "components.file_quarantined",
               "{file} disappeared or became unreadable after it was verified, likely removed by {security_products}");
REBOOT_MESSAGE(kFileVanished, "components.file_vanished",
               "{file} disappeared, became unreadable or changed after it was verified");
REBOOT_MESSAGE(kHelperQuarantined, "components.helper_quarantined",
               "The launcher helper {file} disappeared or became unreadable, likely quarantined by "
               "{security_products}");
REBOOT_MESSAGE(kHelperVanished, "components.helper_vanished",
               "The launcher helper {file} disappeared, became unreadable or changed");

REBOOT_MESSAGE(kHeldFileMissing, "components.held_file_missing", "{file} is missing");
REBOOT_MESSAGE(kHeldFileAccessDenied, "components.held_file_access_denied", "Access to {file} is denied");
REBOOT_MESSAGE(kFileInUse, "components.file_in_use", "{file} is in use by another program");
REBOOT_MESSAGE(kHeldFileUnreadable, "components.held_file_unreadable", "Cannot read {file}");
REBOOT_MESSAGE(kHeldFileChanged, "components.held_file_changed",
               "{file} no longer matches the version that was verified");

REBOOT_MESSAGE(kBundledAssetMissing, "components.bundled_asset_missing", "The installation is missing {path}");

}  // namespace reboot::components
