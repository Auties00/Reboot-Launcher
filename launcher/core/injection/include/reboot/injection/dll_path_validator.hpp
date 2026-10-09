#pragma once

#include <optional>
#include <span>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/result_fwd.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/injection/pinned_dll.hpp"

namespace rb::ports {
class IFileSystem;
}

namespace rb::injection {

// Capabilities: dll-injection.dll-set-resolution, settings-storage.+76.
// Checks run in this order, each only once the previous passed: empty, name (.dll, compared
// case-insensitively), the read (missing or unreadable, keeping the read's OS code and cause),
// PE32+ x64, then the image's DLL characteristic. Failures are injection.dll_* diagnostics.
class DllPathValidator {
public:
    explicit DllPathValidator(ports::IFileSystem& fs) : fs_(fs) {}

    // Reads the file, so the engine calls it on the WorkerPool. The digest is of the bytes read
    // here and is not the integrity check: play re-hashes through its IntegrityHold.
    [[nodiscard]] Result<PinnedDll> validate(const NativePath& path) const;

    // PlaySettings::custom_auth_dll validated; nullopt when none is set, meaning our DLL only.
    [[nodiscard]] Result<std::optional<PinnedDll>> resolve(const std::optional<NativePath>& custom_auth_dll) const;

    // The checks that need no disk access, for settings edits. `path` names the file in messages.
    [[nodiscard]] static Result<void> check_name(const NativePath& path);
    [[nodiscard]] static Result<void> check_image(const NativePath& path, std::span<const u8> image);

private:
    ports::IFileSystem& fs_;
};

}  // namespace rb::injection
