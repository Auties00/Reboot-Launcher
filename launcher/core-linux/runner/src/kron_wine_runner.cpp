#include "reboot/os_linux/runner/kron_wine_runner.hpp"

#include <filesystem>

#include "messages.hpp"
#include "runtime_files.hpp"

namespace rb::os_linux::runner {

Result<KronWineRunner> KronWineRunner::resolve(const NativePath& runtime_dir) {
    const auto root = archive_root(runtime_dir, kWineLoader);
    if (!root) return std::unexpected(root.error());

    const auto missing = [&](std::string_view file) {
        return make_diag(ErrorDomain::Platform, kWineMissing)
            .arg("path", runtime_dir)
            .arg("file", file)
            .kind(ErrorKind::NotFound)
            .fail();
    };
    for (const std::string_view file : {kWineLoader, kWineServer}) {
        const auto status = status_of(*root / file);
        if (!status) return std::unexpected(status.error());
        if (!is_executable_file(*status)) return missing(file);
    }
    const auto dlls = status_of(*root / kWindowsDllDir);
    if (!dlls) return std::unexpected(dlls.error());
    if (!std::filesystem::is_directory(*dlls)) return missing(kWindowsDllDir);

    return KronWineRunner{*root, *root / kWineLoader};
}

ports::RuntimeLayout KronWineRunner::to_runtime_layout() const {
    ports::RuntimeLayout out;
    out.root = root;
    out.entry = wine;
    return out;
}

}  // namespace rb::os_linux::runner
