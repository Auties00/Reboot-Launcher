#pragma once

#include <span>

#include "reboot/base/native_path.hpp"
#include "reboot/base/result_fwd.hpp"
#include "reboot/base/types.hpp"

namespace rb::ports {
class IFileSystem;
}

namespace rb::injection {

// Capabilities: dll-injection.dll-set-resolution, settings-storage.+76.
// Checks run in this order, each only once the previous passed: empty, name (.dll, compared
// case-insensitively), the header reads (missing or unreadable, keeping the read's OS code and
// cause), PE32+ x64, then the image's DLL characteristic. Failures are injection.dll_* diagnostics.
class DllPathValidator {
public:
    explicit DllPathValidator(ports::IFileSystem& fs) : fs_(fs) {}

    // Reads only the PE headers, so the engine calls it on the WorkerPool. No digest: play pins
    // the DLL through its IntegrityHold.
    [[nodiscard]] Result<void> validate(const NativePath& path) const;

    // The checks that need no disk access, for settings edits. `path` names the file in messages.
    [[nodiscard]] static Result<void> check_name(const NativePath& path);
    [[nodiscard]] static Result<void> check_image(const NativePath& path, std::span<const u8> image);

private:
    ports::IFileSystem& fs_;
};

}  // namespace rb::injection
