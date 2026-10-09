#pragma once

#include <optional>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::ports {

enum class InstallKind : u8 { Velopack, Portable, AppBundle, AppImage, Tarball, Dev };

class IPlatformPaths {
public:
    virtual ~IPlatformPaths() = default;

    [[nodiscard]] virtual NativePath default_data_root() const = 0;
    [[nodiscard]] virtual NativePath default_cache_root() const = 0;
    [[nodiscard]] virtual NativePath default_logs_root() const = 0;
    // Directory that holds the engine's AF_UNIX socket; unused on Windows.
    [[nodiscard]] virtual NativePath ipc_runtime_base() const = 0;
    [[nodiscard]] virtual NativePath exe_dir() const = 0;
    [[nodiscard]] virtual InstallKind install_kind() const = 0;
    [[nodiscard]] virtual std::optional<NativePath> velopack_package_dir() const = 0;
};

}  // namespace rb::ports
