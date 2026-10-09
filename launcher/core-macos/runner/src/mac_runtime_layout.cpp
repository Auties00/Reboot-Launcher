#include "reboot/os_macos/runner/mac_runtime_layout.hpp"

#include <filesystem>
#include <system_error>

#include "messages.hpp"
#include "reboot/posix/posix_error.hpp"

namespace rb::os_macos::runner {

namespace {

namespace fs = std::filesystem;

// A missing entry is a not_found status, not an error.
Result<fs::file_status> status_of(const NativePath& path) {
    std::error_code error;
    const fs::file_status status = fs::status(path, error);
    if (error && status.type() != fs::file_type::not_found)
        return make_diag(ErrorDomain::Platform, kRuntimeReadFailed)
            .arg("path", path)
            .os(posix::errno_error(error.value()))
            .fail();
    return status;
}

bool is_executable(fs::perms perms) {
    return (perms & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) != fs::perms::none;
}

}  // namespace

Result<MacRuntimeLayout> MacRuntimeLayout::resolve(const NativePath& runtime_dir) {
    MacRuntimeLayout layout{runtime_dir, runtime_dir / kWineLoader};

    const auto wine = status_of(layout.wine);
    if (!wine) return std::unexpected(wine.error());
    if (!fs::is_regular_file(*wine) || !is_executable(wine->permissions()))
        return make_diag(ErrorDomain::Platform, kWineLoaderMissing)
            .arg("path", runtime_dir)
            .kind(ErrorKind::NotFound)
            .fail();

    const auto require_dxmt = [&](const NativePath& dir, std::string_view file) -> Result<void> {
        const auto status = status_of(dir / file);
        if (!status) return std::unexpected(status.error());
        if (!fs::is_regular_file(*status))
            return make_diag(ErrorDomain::Platform, kDxmtMissing)
                .arg("path", runtime_dir)
                .arg("file", file)
                .kind(ErrorKind::NotFound)
                .fail();
        return {};
    };
    for (const std::string_view dll : kDxmtDlls)
        if (auto found = require_dxmt(runtime_dir / kWindowsDllDir, dll); !found) return std::unexpected(found.error());
    if (auto found = require_dxmt(runtime_dir / kUnixLibDir, kDxmtUnixLib); !found)
        return std::unexpected(found.error());

    return layout;
}

ports::RuntimeLayout MacRuntimeLayout::to_runtime_layout() const {
    ports::RuntimeLayout out;
    out.root = root;
    out.entry = wine;
    return out;
}

}  // namespace rb::os_macos::runner
