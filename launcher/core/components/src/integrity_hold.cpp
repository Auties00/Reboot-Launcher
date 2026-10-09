#include "reboot/components/integrity_hold.hpp"

#include <array>
#include <cerrno>

#include "messages.hpp"
#include "reboot/foundation/sha256.hpp"

namespace rb::components {

namespace {

// Win32 codes, spelled out because core includes no OS headers.
constexpr i64 kWinFileNotFound = 2;
constexpr i64 kWinPathNotFound = 3;
constexpr i64 kWinAccessDenied = 5;
constexpr i64 kWinSharingViolation = 32;
constexpr i64 kWinLockViolation = 33;
// What a real-time scanner returns for a file it blocks or has just removed.
constexpr i64 kWinVirusInfected = 225;
constexpr i64 kWinVirusDeleted = 226;

#if defined(_WIN32)
constexpr bool kHostIsWindows = true;
#else
constexpr bool kHostIsWindows = false;
#endif

[[nodiscard]] std::optional<HoldFailureKind> classify_windows(i64 code) noexcept {
    switch (code) {
        case kWinFileNotFound:
        case kWinPathNotFound: return HoldFailureKind::Missing;
        case kWinAccessDenied:
        case kWinVirusInfected:
        case kWinVirusDeleted: return HoldFailureKind::AccessDenied;
        case kWinSharingViolation:
        case kWinLockViolation: return HoldFailureKind::InUse;
        default: return std::nullopt;
    }
}

[[nodiscard]] std::optional<HoldFailureKind> classify_posix(i64 code) noexcept {
    if (code == ENOENT) return HoldFailureKind::Missing;
    if (code == EACCES || code == EPERM) return HoldFailureKind::AccessDenied;
    return std::nullopt;
}

using Hashed = std::expected<Sha256Digest, HoldFailure>;

[[nodiscard]] Hashed hash_held(ports::HeldFile& file, const NativePath& path) {
    Sha256 hasher;
    std::array<u8, 64 * 1024> buffer{};
    while (true) {
        auto read = file.read(buffer);
        if (!read) {
            const HoldFailureKind kind = classify_hold_failure(read.error());
            // The file opened, so a failed read is never absence.
            return std::unexpected(HoldFailure{kind == HoldFailureKind::Missing ? HoldFailureKind::ReadFailed : kind,
                                               path, std::move(read.error())});
        }
        if (*read == 0) break;
        hasher.update(std::span<const u8>(buffer.data(), *read));
    }
    return hasher.finish();
}

[[nodiscard]] std::expected<IntegrityHold, HoldFailure> open_and_hash(ports::IFileSystem& fs, NativePath path,
                                                                     const Sha256Digest* expected,
                                                                     auto make_hold) {
    auto file = fs.open_deny_write(path);
    if (!file) {
        const HoldFailureKind kind = classify_hold_failure(file.error());
        return std::unexpected(HoldFailure{kind, std::move(path), std::move(file.error())});
    }
    Hashed digest = hash_held(*file, path);
    if (!digest) return std::unexpected(std::move(digest.error()));
    if (expected != nullptr && *digest != *expected)
        return std::unexpected(HoldFailure{HoldFailureKind::Mismatch, std::move(path), std::nullopt});
    return make_hold(std::move(path), *digest, std::move(*file));
}

}  // namespace

HoldFailureKind classify_hold_failure(const Diagnostic& cause) noexcept {
    if (cause.kind == ErrorKind::NotFound) return HoldFailureKind::Missing;
    if (cause.kind == ErrorKind::Conflict) return HoldFailureKind::InUse;
    if (cause.os_error) {
        const bool windows_code = kHostIsWindows || cause.os_error->origin == SystemError::Origin::GuestWindows;
        const std::optional<HoldFailureKind> kind =
            windows_code ? classify_windows(cause.os_error->code) : classify_posix(cause.os_error->code);
        if (kind) return *kind;
    }
    return HoldFailureKind::ReadFailed;
}

Diagnostic to_diagnostic(const HoldFailure& failure) {
    MessageId message = kHeldFileUnreadable;
    ErrorKind kind = ErrorKind::Generic;
    bool retryable = false;
    switch (failure.kind) {
        case HoldFailureKind::Missing:
            message = kHeldFileMissing;
            kind = ErrorKind::NotFound;
            break;
        case HoldFailureKind::AccessDenied: message = kHeldFileAccessDenied; break;
        case HoldFailureKind::InUse:
            message = kFileInUse;
            kind = ErrorKind::Conflict;
            retryable = true;
            break;
        case HoldFailureKind::ReadFailed: message = kHeldFileUnreadable; break;
        case HoldFailureKind::Mismatch: message = kHeldFileChanged; break;
    }
    DiagBuilder builder =
        make_diag(ErrorDomain::Components, message).arg("file", failure.file).kind(kind).retryable(retryable);
    if (failure.cause) std::move(builder).cause(*failure.cause);
    return std::move(builder).build();
}

std::expected<IntegrityHold, HoldFailure> IntegrityHold::acquire(ports::IFileSystem& fs, NativePath path,
                                                                 const Sha256Digest& expected) {
    return open_and_hash(fs, std::move(path), &expected, [](NativePath held, const Sha256Digest& digest,
                                                            ports::HeldFile file) {
        return IntegrityHold(std::move(held), digest, std::move(file));
    });
}

std::expected<IntegrityHold, HoldFailure> IntegrityHold::acquire(ports::IFileSystem& fs, NativePath path) {
    return open_and_hash(fs, std::move(path), nullptr, [](NativePath held, const Sha256Digest& digest,
                                                          ports::HeldFile file) {
        return IntegrityHold(std::move(held), digest, std::move(file));
    });
}

}  // namespace rb::components
