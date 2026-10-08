#include "reboot/storage/data_root.hpp"

#include <array>

#include "messages.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/ports/platform_paths.hpp"

namespace reboot::storage {

Result<DataRootReport> prepare_data_root(const AppLayout& layout, const InstallLayout& install,
                                         const ports::IPlatformPaths& paths, ports::IFileSystem& fs) {
    if (const std::optional<NativePath> package = paths.velopack_package_dir();
        package && is_inside(layout.root(), *package))
        return invalid_input(msg::kRootInsidePackage).arg("root", layout.root()).arg("package", *package).fail();

    DataRootReport report;
    const std::array directories{
        layout.settings_file().parent_path(), layout.frontend_dir(),        layout.library_file().parent_path(),
        layout.state_file().parent_path(),    layout.host_identity_dir(),   layout.components_dir(),
        layout.prefixes_dir(),                layout.backend_dir(),         layout.catalog_cache().parent_path(),
        layout.logs_dir()};
    for (const NativePath& directory : directories) {
        if (Result<void> created = fs.create_dirs_owner_only(directory); !created) {
            report.mode = StorageMode::InMemory;
            report.reason = make_diag(ErrorDomain::Storage, msg::kRootNotWritable)
                                .arg("path", directory)
                                .cause(std::move(created.error()))
                                .build();
            break;
        }
    }

    const std::array install_files{install.backend_exe, install.game_server_exe, install.backend_content_dir,
                                   install.bundled_catalog, install.bundled_manifest};
    for (const NativePath& file : install_files)
        if (!fs.revision(file)) report.missing_install_files.push_back(file);
    return report;
}

}  // namespace reboot::storage
