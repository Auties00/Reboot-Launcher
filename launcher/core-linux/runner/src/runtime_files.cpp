#include "runtime_files.hpp"

#include <array>
#include <cerrno>
#include <optional>
#include <system_error>

#include <fcntl.h>
#include <unistd.h>

#include "messages.hpp"
#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/unique_fd.hpp"

namespace rb::os_linux::runner {

namespace fs = std::filesystem;

namespace {

Diagnostic read_failed(const NativePath& path, int error) {
    return make_diag(ErrorDomain::Platform, kRuntimeReadFailed).arg("path", path).os(posix::errno_error(error));
}

}  // namespace

Result<fs::file_status> status_of(const NativePath& path) {
    std::error_code error;
    const fs::file_status status = fs::status(path, error);
    if (error && status.type() != fs::file_type::not_found) return std::unexpected(read_failed(path, error.value()));
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
        // A dangling symlink is no directory, not a read failure.
        std::error_code type_error;
        if (entry->is_directory(type_error)) only = entry->path();
    }
    if (error) return std::unexpected(read_failed(runtime_dir, error.value()));
    if (entries == 1 && only) return *only;
    return runtime_dir;
}

Result<std::string> read_text(const NativePath& path) {
    posix::UniqueFd fd;
    do {
        fd.reset(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    } while (!fd.valid() && errno == EINTR);
    if (!fd.valid()) return std::unexpected(read_failed(path, errno));

    std::string text;
    std::array<char, 4096> chunk{};
    for (;;) {
        const ssize_t got = ::read(fd.get(), chunk.data(), chunk.size());
        if (got < 0 && errno == EINTR) continue;
        // EISDIR for a directory, which a stream would read as empty.
        if (got < 0) return std::unexpected(read_failed(path, errno));
        if (got == 0) return text;
        text.append(chunk.data(), static_cast<std::size_t>(got));
    }
}

}  // namespace rb::os_linux::runner
