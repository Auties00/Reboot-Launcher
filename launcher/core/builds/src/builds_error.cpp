#include "builds_error.hpp"

#include <utility>

#include "messages.hpp"
#include "pe_error.hpp"
#include "reboot/builds/pe_version_reader.hpp"

namespace reboot::builds {

namespace {

[[nodiscard]] DiagBuilder builds_diag(MessageId message) { return make_diag(ErrorDomain::Builds, message); }

[[nodiscard]] NativePath path_of(const BuildsError& error) { return error.path.value_or(NativePath{}); }

[[nodiscard]] std::string version_text(const std::optional<GameVersion>& version) {
    return version ? version->canonical() : std::string{};
}

[[nodiscard]] DiagBuilder builder_for(const BuildsError& error) {
    switch (error.code) {
        case BuildsErrorCode::NameEmpty: return builds_diag(msg::kNameEmpty).kind(ErrorKind::InvalidInput);
        case BuildsErrorCode::NameTaken:
            return builds_diag(msg::kNameTaken).arg("name", error.name).kind(ErrorKind::Conflict);
        case BuildsErrorCode::PathNotAbsolute:
            return builds_diag(msg::kPathNotAbsolute).arg("path", path_of(error)).kind(ErrorKind::InvalidInput);
        case BuildsErrorCode::PathMissing:
            return builds_diag(msg::kPathMissing).arg("path", path_of(error)).kind(ErrorKind::NotFound);
        case BuildsErrorCode::NotADirectory:
            return builds_diag(msg::kNotADirectory).arg("path", path_of(error)).kind(ErrorKind::InvalidInput);
        case BuildsErrorCode::AlreadyRegistered:
            return builds_diag(msg::kAlreadyRegistered)
                .arg("path", path_of(error))
                .arg("name", error.name)
                .kind(ErrorKind::Conflict);
        case BuildsErrorCode::OverlapsBuild:
            return builds_diag(msg::kOverlapsBuild)
                .arg("path", path_of(error))
                .arg("name", error.name)
                .kind(ErrorKind::Conflict);
        case BuildsErrorCode::InsideInstallDir:
            return builds_diag(msg::kInsideInstallDir).arg("path", path_of(error)).kind(ErrorKind::InvalidInput);
        case BuildsErrorCode::NotFound:
            return builds_diag(msg::kNotFound)
                .arg("build", error.build ? format_uuid(error.build->value) : std::string{})
                .kind(ErrorKind::NotFound);
        case BuildsErrorCode::InUse:
            return builds_diag(msg::kInUse).arg("name", error.name).arg("count", error.count).kind(ErrorKind::Conflict);
        case BuildsErrorCode::DestinationNotEmpty:
            return builds_diag(msg::kDestinationNotEmpty).arg("path", path_of(error)).kind(ErrorKind::Conflict);
        case BuildsErrorCode::DestinationBusy:
            return builds_diag(msg::kDestinationBusy).arg("path", path_of(error)).kind(ErrorKind::Conflict);
        case BuildsErrorCode::VolumeReadOnly:
            return builds_diag(msg::kVolumeReadOnly).arg("path", path_of(error)).kind(ErrorKind::Unsupported);
        case BuildsErrorCode::VolumeNetwork:
            return builds_diag(msg::kVolumeNetwork).arg("path", path_of(error)).kind(ErrorKind::Unsupported);
        case BuildsErrorCode::VolumeRemovable:
            return builds_diag(msg::kVolumeRemovable).arg("path", path_of(error)).kind(ErrorKind::Unsupported);
        case BuildsErrorCode::VolumeFat:
            return builds_diag(msg::kVolumeFat)
                .arg("path", path_of(error))
                .arg("fs_type", error.fs_type)
                .kind(ErrorKind::Unsupported);
        case BuildsErrorCode::InsufficientSpace:
            return builds_diag(msg::kInsufficientSpace)
                .arg("path", path_of(error))
                .arg("needed_bytes", error.needed_bytes.value_or(0))
                .arg("free_bytes", error.free_bytes.value_or(0))
                .kind(ErrorKind::Conflict);
        case BuildsErrorCode::BuildUnavailable:
            return builds_diag(msg::kBuildUnavailable).arg("entry", error.entry).kind(ErrorKind::NotFound);
        case BuildsErrorCode::DownloadFailed:
            return builds_diag(msg::kDownloadFailed).arg("entry", error.entry).retryable();
        case BuildsErrorCode::ChecksumMismatch:
            return builds_diag(msg::kChecksumMismatch).arg("entry", error.entry).retryable();
        case BuildsErrorCode::UnsupportedArchive:
            return builds_diag(msg::kUnsupportedArchive).arg("path", path_of(error)).kind(ErrorKind::Unsupported);
        case BuildsErrorCode::CorruptArchive:
            return builds_diag(msg::kCorruptArchive)
                .arg("path", path_of(error))
                .arg("archive_entry", error.archive_entry);
        case BuildsErrorCode::UnsafeEntryPath:
            return builds_diag(msg::kUnsafeEntryPath).arg("archive_entry", error.archive_entry);
        case BuildsErrorCode::MissingShipping:
            return builds_diag(msg::kMissingShipping).arg("path", path_of(error)).kind(ErrorKind::NotFound);
        case BuildsErrorCode::MultipleShipping:
            return builds_diag(msg::kMultipleShipping)
                .arg("count", error.count)
                .arg("path", path_of(error))
                .kind(ErrorKind::Conflict);
        case BuildsErrorCode::ShippingNotFound:
            return builds_diag(msg::kShippingNotFound)
                .arg("other_path", error.other_path.value_or(NativePath{}))
                .arg("path", path_of(error))
                .kind(ErrorKind::InvalidInput);
        case BuildsErrorCode::UnsupportedVersion:
            return builds_diag(msg::kUnsupportedVersion)
                .arg("version", version_text(error.version))
                .kind(ErrorKind::Unsupported);
        case BuildsErrorCode::VersionMismatch:
            return builds_diag(msg::kVersionMismatch)
                .arg("path", path_of(error))
                .arg("found_version", version_text(error.found_version))
                .arg("version", version_text(error.version))
                .kind(ErrorKind::Conflict);
        case BuildsErrorCode::UnknownInstallFolder:
            return builds_diag(msg::kUnknownInstallFolder).arg("path", path_of(error)).kind(ErrorKind::NotFound);
        case BuildsErrorCode::RemoveFailed: return builds_diag(msg::kRemoveFailed).arg("path", path_of(error));
        case BuildsErrorCode::Io: return builds_diag(msg::kIo).arg("path", path_of(error));
        case BuildsErrorCode::Cancelled: return builds_diag(msg::kCancelled).kind(ErrorKind::Cancelled);
    }
    return builds_diag(msg::kIo).arg("path", path_of(error));
}

}  // namespace

Diagnostic to_diagnostic(const BuildsError& error) {
    DiagBuilder builder = builder_for(error);
    if (error.os_error) std::move(builder).os(*error.os_error);
    if (error.cause) std::move(builder).cause(*error.cause);
    return std::move(builder).build();
}

Diagnostic to_diagnostic(const PeError& error) {
    DiagBuilder builder = [&]() -> DiagBuilder {
        switch (error.code) {
            case PeErrorCode::NotPe: return builds_diag(msg::kPeNotPe).kind(ErrorKind::Unsupported);
            case PeErrorCode::Malformed: return builds_diag(msg::kPeMalformed).arg("offset", error.offset);
            case PeErrorCode::NoVersionResource:
                return builds_diag(msg::kPeNoVersionResource).kind(ErrorKind::NotFound);
            case PeErrorCode::ResourceTooLarge:
                return builds_diag(msg::kPeResourceTooLarge).arg("limit", u64{kVersionResourceCap});
            case PeErrorCode::ReadFailed: return builds_diag(msg::kPeReadFailed);
            case PeErrorCode::Cancelled: return builds_diag(msg::kCancelled).kind(ErrorKind::Cancelled);
        }
        return builds_diag(msg::kPeReadFailed);
    }();
    if (error.cause) std::move(builder).cause(*error.cause);
    return std::move(builder).build();
}

}  // namespace reboot::builds
