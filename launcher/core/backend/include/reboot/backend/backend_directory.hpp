#pragma once

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/paths.hpp"

namespace reboot::backend {

// Capabilities: auth-backend.open-directory.
// The embedded backend's folders. "Show files" opens data_dir(), which is per user and writable;
// nothing under the install directory is meant to be edited.
class BackendDirectory {
public:
    BackendDirectory(const AppLayout& layout, const InstallLayout& install)
        : data_dir_(layout.backend_dir()), content_dir_(install.backend_content_dir) {}

    // REBOOT_BACKEND_DATA; the backend's only mutable state.
    [[nodiscard]] const NativePath& data_dir() const noexcept { return data_dir_; }
    // REBOOT_BACKEND_CONTENT; read-only.
    [[nodiscard]] const NativePath& content_dir() const noexcept { return content_dir_; }

private:
    NativePath data_dir_;
    NativePath content_dir_;
};

}  // namespace reboot::backend
