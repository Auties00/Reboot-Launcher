#pragma once

#include <optional>
#include <utility>

#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/platform_paths.hpp"

namespace rb::testing {

// An absolute root that names no real directory: C:/reboot-test on Windows, /reboot-test elsewhere.
[[nodiscard]] NativePath default_fake_root();

// Covers no capability ids (decision testing-strategy).
// Every root sits under `base` (data/, cache/, logs/, run/, app/), so nothing reaches the real home
// directory. The install kind is Dev unless a test sets another.
class FakePlatformPaths final : public ports::IPlatformPaths {
public:
    explicit FakePlatformPaths(NativePath base = default_fake_root()) : base_(std::move(base)) {}

    [[nodiscard]] NativePath default_data_root() const override { return base_ / "data"; }
    [[nodiscard]] NativePath default_cache_root() const override { return base_ / "cache"; }
    [[nodiscard]] NativePath default_logs_root() const override { return base_ / "logs"; }
    [[nodiscard]] NativePath ipc_runtime_base() const override { return base_ / "run"; }
    [[nodiscard]] NativePath exe_dir() const override { return base_ / "app"; }
    [[nodiscard]] ports::InstallKind install_kind() const override { return install_kind_; }
    [[nodiscard]] std::optional<NativePath> velopack_package_dir() const override { return velopack_dir_; }

    void set_install_kind(ports::InstallKind kind) noexcept { install_kind_ = kind; }
    void set_velopack_package_dir(std::optional<NativePath> dir) { velopack_dir_ = std::move(dir); }
    [[nodiscard]] const NativePath& base() const noexcept { return base_; }

private:
    NativePath base_;
    ports::InstallKind install_kind_ = ports::InstallKind::Dev;
    std::optional<NativePath> velopack_dir_;
};

}  // namespace rb::testing
