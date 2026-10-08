#include "runtime_files.hpp"

#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>

#include "messages.hpp"
#include "reboot/posix/posix_error.hpp"

namespace reboot::os_linux::runner {

namespace fs = std::filesystem;

namespace {

Diagnostic read_failed(const NativePath& path, const std::error_code& error) {
    return make_diag(ErrorDomain::Platform, kRuntimeReadFailed).arg("path", path).os(posix::errno_error(error.value()));
}

}  // namespace

Result<fs::file_status> status_of(const NativePath& path) {
    std::error_code error;
    const fs::file_status status = fs::status(path, error);
    if (error && status.type() != fs::file_type::not_found) return std::unexpected(read_failed(path, error));
    return status;
}

bool is_executable_file(const fs::file_status& status) {
    constexpr fs::perms kAnyExec = fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec;
    return fs::is_regular_file(status) && (status.permissions() & kAnyExec) != fs::perms::none;
}

Result<NativePath> archive_root(const NativePath& runtime_dir, std::string_view marker) {
    const auto direct = status_of(runtime_dir / marker);
    if (!direct) return std::unexpected(direct.error());
    if (fs::exists(*direct)) return runtime_dir;

    std::error_code error;
    std::optional<NativePath> only;
    std::size_t entries = 0;
    fs::directory_iterator entry(runtime_dir, error);
    for (; !error && entry != fs::directory_iterator(); entry.increment(error)) {
        ++entries;
        if (entry->is_directory(error)) only = entry->path();
    }
    if (error) return std::unexpected(read_failed(runtime_dir, error));
    if (entries == 1 && only) return *only;
    return runtime_dir;
}

Result<std::string> read_text(const NativePath& path) {
    std::ifstream in(path, std::ios::binary);
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (!in.is_open() || in.bad())
        return make_diag(ErrorDomain::Platform, kRuntimeReadFailed).arg("path", path).fail();
    return text;
}

}  // namespace reboot::os_linux::runner
