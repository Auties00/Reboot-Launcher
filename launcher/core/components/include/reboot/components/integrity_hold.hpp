#pragma once

#include <expected>
#include <optional>
#include <utility>

#include "reboot/components/component_ref.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/file_system.hpp"

namespace reboot::components {

// Classified from the port's Diagnostic, so a file someone else has open is never blamed on
// security software. Only Missing and AccessDenied ask ISecurityProductProbe.
// - Missing: ErrorKind::NotFound.
// - InUse: ErrorKind::Conflict, or a host sharing or lock violation (a scanner or a leftover game).
// - AccessDenied: a host access-denied code (ERROR_ACCESS_DENIED, EACCES, EPERM).
// - ReadFailed: any other open or read failure.
// - Mismatch: the bytes read do not hash to the expected digest.
enum class HoldFailureKind : u8 { Missing, AccessDenied, InUse, ReadFailed, Mismatch };

struct HoldFailure {
    HoldFailureKind kind{};
    NativePath file;
    // The port's error; absent for Mismatch.
    std::optional<Diagnostic> cause;
};

[[nodiscard]] HoldFailureKind classify_hold_failure(const Diagnostic& cause) noexcept;

// components.held_file_missing, held_file_access_denied, file_in_use, held_file_unreadable or
// held_file_changed, with `cause` attached.
[[nodiscard]] Diagnostic to_diagnostic(const HoldFailure& failure);

// Capabilities: dll-injection.prelaunch-verification.
// Opens a file through IFileSystem::open_deny_write and hashes it through that same handle, kept
// until the session reports the load confirmed.
// - Windows: the handle denies writes, renames and deletes, so the bytes checked are the bytes
//   loaded.
// - macOS and Linux: the engine's fd denies nothing and is invisible to wineserver share modes, so
//   it protects nothing; under Wine winhost opens each file with its own share-denied handle
//   inside the prefix and checks InjectSpec::sha256. There the engine uses it only to learn a
//   file's digest.
class IntegrityHold {
public:
    // Blocking; runs on the WorkerPool.
    [[nodiscard]] static std::expected<IntegrityHold, HoldFailure> acquire(ports::IFileSystem& fs, NativePath path,
                                                                           const Sha256Digest& expected);
    // For a file with no published digest, such as a custom auth DLL; never fails with Mismatch.
    [[nodiscard]] static std::expected<IntegrityHold, HoldFailure> acquire(ports::IFileSystem& fs, NativePath path);

    IntegrityHold(IntegrityHold&& other) noexcept
        : path_(std::move(other.path_)),
          sha256_(other.sha256_),
          file_(std::move(other.file_)),
          held_(std::exchange(other.held_, false)) {}
    IntegrityHold& operator=(IntegrityHold&& other) noexcept {
        path_ = std::move(other.path_);
        sha256_ = other.sha256_;
        file_ = std::move(other.file_);
        held_ = std::exchange(other.held_, false);
        return *this;
    }
    IntegrityHold(const IntegrityHold&) = delete;
    IntegrityHold& operator=(const IntegrityHold&) = delete;
    ~IntegrityHold() = default;

    [[nodiscard]] const NativePath& path() const noexcept { return path_; }
    // The digest computed through the held handle.
    [[nodiscard]] const Sha256Digest& sha256() const noexcept { return sha256_; }
    [[nodiscard]] bool held() const noexcept { return held_; }

    void release() noexcept {
        file_ = ports::HeldFile{};
        held_ = false;
    }

private:
    IntegrityHold(NativePath path, const Sha256Digest& sha256, ports::HeldFile file)
        : path_(std::move(path)), sha256_(sha256), file_(std::move(file)) {}

    NativePath path_;
    Sha256Digest sha256_{};
    ports::HeldFile file_;
    bool held_ = true;
};

}  // namespace reboot::components
