#include "messages.hpp"

namespace rb::builds::msg {

REBOOT_MESSAGE(kNameEmpty, "builds.name_empty", "A build name cannot be empty");
REBOOT_MESSAGE(kNameTaken, "builds.name_taken", "A build named {name} already exists");
REBOOT_MESSAGE(kPathNotAbsolute, "builds.path_not_absolute", "{path} is not an absolute path");
REBOOT_MESSAGE(kPathMissing, "builds.path_missing", "{path} does not exist");
REBOOT_MESSAGE(kNotADirectory, "builds.not_a_directory", "{path} is not a folder");
REBOOT_MESSAGE(kAlreadyRegistered, "builds.already_registered", "{path} is already in the library as {name}");
REBOOT_MESSAGE(kOverlapsBuild, "builds.overlaps_build", "{path} overlaps the folder of build {name}");
REBOOT_MESSAGE(kInsideInstallDir, "builds.inside_install_dir", "{path} is inside the launcher's installation folder");
REBOOT_MESSAGE(kNotFound, "builds.not_found", "There is no build with id {build}");
REBOOT_MESSAGE(kInUse, "builds.in_use", "{name} is in use by {count} running sessions");
REBOOT_MESSAGE(kDestinationNotEmpty, "builds.destination_not_empty", "{path} is not empty");
REBOOT_MESSAGE(kDestinationBusy, "builds.destination_busy", "Another install is already writing to {path}");
REBOOT_MESSAGE(kVolumeReadOnly, "builds.volume_read_only", "{path} is on a read-only volume");
REBOOT_MESSAGE(kVolumeNetwork, "builds.volume_network", "{path} is on a network volume");
REBOOT_MESSAGE(kVolumeRemovable, "builds.volume_removable", "{path} is on a removable volume");
REBOOT_MESSAGE(kVolumeFat, "builds.volume_fat",
               "{path} is on a {fs_type} volume, which cannot hold the game's files over 4 GiB");
REBOOT_MESSAGE(kInsufficientSpace, "builds.insufficient_space",
               "{path} needs {needed_bytes} bytes free but has {free_bytes}");
REBOOT_MESSAGE(kBuildUnavailable, "builds.build_unavailable", "Build {entry} is no longer available from its host");
REBOOT_MESSAGE(kDownloadFailed, "builds.download_failed", "Cannot download build {entry}");
REBOOT_MESSAGE(kChecksumMismatch, "builds.checksum_mismatch",
               "The download of {entry} does not match its published checksum");
REBOOT_MESSAGE(kUnsupportedArchive, "builds.unsupported_archive",
               "{path} uses an archive format or coder this launcher cannot read");
REBOOT_MESSAGE(kCorruptArchive, "builds.corrupt_archive", "{path} is damaged at entry {archive_entry}");
REBOOT_MESSAGE(kUnsafeEntryPath, "builds.unsafe_entry_path",
               "The archive entry {archive_entry} would be written outside the build folder");
REBOOT_MESSAGE(kMissingShipping, "builds.missing_shipping",
               "No FortniteClient-Win64-Shipping.exe was found under {path}");
REBOOT_MESSAGE(kMultipleShipping, "builds.multiple_shipping", "{count} game executables were found under {path}");
REBOOT_MESSAGE(kShippingNotFound, "builds.shipping_not_found",
               "{other_path} is not one of the game executables found under {path}");
REBOOT_MESSAGE(kUnsupportedVersion, "builds.unsupported_version", "Version {version} is not supported");
REBOOT_MESSAGE(kVersionMismatch, "builds.version_mismatch", "{path} holds version {found_version}, not {version}");
REBOOT_MESSAGE(kUnknownInstallFolder, "builds.unknown_install_folder",
               "{path} is not a folder an install left unregistered");
REBOOT_MESSAGE(kRemoveFailed, "builds.remove_failed", "Cannot remove {path}");
REBOOT_MESSAGE(kIo, "builds.io", "Cannot access {path}");
REBOOT_MESSAGE(kCancelled, "builds.cancelled", "The build operation was cancelled");

REBOOT_MESSAGE(kPeNotPe, "builds.pe_not_pe", "The file is not a Windows executable");
REBOOT_MESSAGE(kPeMalformed, "builds.pe_malformed", "The executable has a malformed header at offset {offset}");
REBOOT_MESSAGE(kPeNoVersionResource, "builds.pe_no_version_resource", "The executable has no version information");
REBOOT_MESSAGE(kPeResourceTooLarge, "builds.pe_resource_too_large",
               "The executable's version information is larger than {limit} bytes");
REBOOT_MESSAGE(kPeReadFailed, "builds.pe_read_failed", "The executable could not be read");
REBOOT_MESSAGE(kVersionFileUnreadable, "builds.version_file_unreadable", "No version could be read from {path}");

REBOOT_MESSAGE(kNoReleaseMarker, "builds.no_release_marker", "{path} names no Fortnite release");
REBOOT_MESSAGE(kUnknownReleaseShape, "builds.unknown_release_shape",
               "The release name {raw} in {path} is not a known version form");
REBOOT_MESSAGE(kUnknownChangelist, "builds.unknown_changelist", "Engine changelist {cl} is not a known release");
REBOOT_MESSAGE(kCatalogVersionMismatch, "builds.catalog_version_mismatch",
               "Build {entry} is listed as {catalog_version} but its files say {version}");
REBOOT_MESSAGE(kWalkIncomplete, "builds.walk_incomplete", "{count} folders under {path} could not be read");
REBOOT_MESSAGE(kCaseCollision, "builds.case_collision",
               "The archive holds both {kept} and {replaced}, which differ only in case; {kept} was kept");

}  // namespace rb::builds::msg
