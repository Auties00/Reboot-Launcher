#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/result_fwd.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::ports {
class IPlatformPaths;
}

namespace reboot {

struct DataRoot {
    NativePath root;
    bool overridden = false;
};

// `env_override` is REBOOT_LAUNCHER_HOME when set.
[[nodiscard]] Result<DataRoot> resolve_data_root(const ports::IPlatformPaths& paths,
                                                 std::optional<std::string_view> env_override);

// Absolute and lexically normal, with symlinks resolved where the path exists.
[[nodiscard]] NativePath canonical_root(const DataRoot& root);

// First 16 lowercase hex digits of sha256 over the UTF-8 (Windows) or raw (POSIX) path bytes.
[[nodiscard]] std::string root_hash16(const NativePath& canonical);

// Cache and logs live under the data root when it is overridden, otherwise at the platform's
// cache and logs roots.
class AppLayout {
public:
    AppLayout(const DataRoot& root, const ports::IPlatformPaths& paths);

    [[nodiscard]] const NativePath& root() const noexcept { return root_; }

    [[nodiscard]] NativePath settings_file() const { return root_ / "config" / "settings.json"; }
    [[nodiscard]] NativePath frontend_dir() const { return root_ / "config" / "frontend"; }
    [[nodiscard]] NativePath library_file() const { return root_ / "data" / "library.json"; }
    [[nodiscard]] NativePath accounts_file() const { return root_ / "data" / "accounts.json"; }
    [[nodiscard]] NativePath host_profiles_file() const { return root_ / "data" / "host-profiles.json"; }
    [[nodiscard]] NativePath state_file() const { return root_ / "state" / "state.json"; }
    [[nodiscard]] NativePath host_identity_dir() const { return root_ / "state" / "host-identity"; }

    [[nodiscard]] NativePath runtime_file() const { return root_ / "state" / "runtime.json"; }
    [[nodiscard]] NativePath resume_file() const { return root_ / "state" / "resume.json"; }
    [[nodiscard]] NativePath update_marker() const { return root_ / "state" / "update-in-progress"; }
    [[nodiscard]] NativePath engine_lock() const { return root_ / "state" / "engine.lock"; }
    [[nodiscard]] NativePath spawn_lock() const { return root_ / "state" / "spawn.lock"; }

    [[nodiscard]] NativePath components_dir() const { return root_ / "data" / "components"; }
    [[nodiscard]] NativePath prefixes_dir() const { return root_ / "data" / "prefixes"; }
    [[nodiscard]] NativePath backend_dir() const { return root_ / "data" / "backend"; }
    [[nodiscard]] NativePath game_server_session_dir(const SessionId& session) const {
        return root_ / "data" / "game-server" / format_uuid(session.value);
    }

    [[nodiscard]] NativePath catalog_cache() const { return cache_ / "catalog.json"; }
    [[nodiscard]] NativePath manifest_cache() const { return cache_ / "manifest.json"; }
    [[nodiscard]] NativePath describe_cache() const { return cache_ / "game-server-describe.json"; }
    [[nodiscard]] const NativePath& logs_dir() const noexcept { return logs_; }

private:
    NativePath root_;
    NativePath cache_;
    NativePath logs_;
};

struct InstallLayout {
    NativePath install_dir;
    NativePath backend_exe;
    NativePath game_server_exe;
    NativePath backend_content_dir;
    NativePath bundled_catalog;
    NativePath bundled_manifest;
};

// `dev_override` points at a build tree during development.
[[nodiscard]] InstallLayout locate_install(const ports::IPlatformPaths& paths, std::optional<NativePath> dev_override);

// Lexical, on normalised absolute paths; a path is inside itself.
[[nodiscard]] bool is_inside(const NativePath& child, const NativePath& parent);

}  // namespace reboot
