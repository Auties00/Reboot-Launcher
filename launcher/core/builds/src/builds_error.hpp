#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace reboot::builds {

enum class BuildsErrorCode : u8 {
    NameEmpty,
    NameTaken,
    PathNotAbsolute,
    PathMissing,
    NotADirectory,
    // The same root, by canonical path or file id, is already in the library.
    AlreadyRegistered,
    // Inside, or containing, the root of another build.
    OverlapsBuild,
    InsideInstallDir,
    NotFound,
    InUse,
    DestinationNotEmpty,
    DestinationBusy,
    VolumeReadOnly,
    VolumeNetwork,
    VolumeRemovable,
    // FAT12/16/32 cannot hold the multi-GiB .pak files.
    VolumeFat,
    InsufficientSpace,
    // HTTP 404 or 410 from the build host.
    BuildUnavailable,
    DownloadFailed,
    ChecksumMismatch,
    UnsupportedArchive,
    CorruptArchive,
    UnsafeEntryPath,
    MissingShipping,
    MultipleShipping,
    // A chosen shipping exe that is not one of the candidates the walk found.
    ShippingNotFound,
    UnsupportedVersion,
    // A relocation target whose files settle another version.
    VersionMismatch,
    UnknownInstallFolder,
    RemoveFailed,
    Io,
    Cancelled,
};

// Package-internal; every public function returns it as a Diagnostic through to_diagnostic.
struct BuildsError {
    BuildsErrorCode code = BuildsErrorCode::Io;
    std::optional<NativePath> path;
    // The registered root a path collides with, or the chosen shipping exe.
    std::optional<NativePath> other_path;
    std::string name;
    std::optional<BuildId> build;
    CatalogEntryId entry;
    // An archive entry name as stored in the archive.
    std::string archive_entry;
    std::string fs_type;
    std::optional<u64> needed_bytes;
    std::optional<u64> free_bytes;
    std::optional<GameVersion> version;
    std::optional<GameVersion> found_version;
    u32 count = 0;
    std::optional<SystemError> os_error;
    std::optional<Diagnostic> cause;
};

[[nodiscard]] Diagnostic to_diagnostic(const BuildsError& error);

}  // namespace reboot::builds
