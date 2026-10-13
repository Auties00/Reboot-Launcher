#include "reboot/posix/token_files.hpp"

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <vector>

#include "exclusive_file.hpp"
#include "messages.hpp"
#include "owner_only_directory.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/unique_fd.hpp"
#include "unistd.hpp"
#include "unix_endpoint_checks.hpp"

namespace rb::posix {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kTokenName = "token";
constexpr std::string_view kDirPrefix = "ctl-";

// The base directory opened without following a link and checked through that fd.
Result<UniqueFd> open_private_base(const NativePath& base) {
    UniqueFd fd{::open(base.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    if (!fd.valid()) {
        const int error = errno;
        // A link fails with ELOOP, a file with ENOTDIR.
        if (error == ELOOP || error == ENOTDIR)
            return make_diag(ErrorDomain::Posix, kNotADirectory).arg("path", base).fail();
        return std::unexpected(call_failed("open", error, base));
    }
    struct stat info {};
    if (::fstat(fd.get(), &info) != 0) return std::unexpected(call_failed("fstat", errno, base));
    const u32 uid = static_cast<u32>(::geteuid());
    const u32 mode = static_cast<u32>(info.st_mode) & 07777U;
    if (info.st_uid != uid || mode != 0700U)
        return make_diag(ErrorDomain::Posix, kDirectoryNotPrivate)
            .arg("path", base)
            .arg("expected_uid", uid)
            .arg("owner_uid", static_cast<u32>(info.st_uid))
            .arg("mode", octal_mode(mode))
            .fail();
    return fd;
}

class PosixTokenFile final : public ports::ITokenFile {
public:
    PosixTokenFile(NativePath directory, std::size_t size)
        : directory_(std::move(directory)), path_(directory_ / kTokenName), size_(size) {}

    ~PosixTokenFile() override {
        // Zeroed first, since the base may sit on a disk rather than a tmpfs.
        const UniqueFd fd{::open(path_.c_str(), O_WRONLY | O_NOFOLLOW | O_CLOEXEC)};
        if (fd.valid()) {
            const std::vector<char> zeros(size_, '\0');
            std::size_t done = 0;
            while (done < zeros.size()) {
                const ssize_t wrote = ::write(fd.get(), zeros.data() + done, zeros.size() - done);
                if (wrote <= 0 && errno != EINTR) break;
                if (wrote > 0) done += static_cast<std::size_t>(wrote);
            }
        }
        const int unlinked = ::unlink(path_.c_str()) == 0 ? 0 : errno;
        if (unlinked != 0 && unlinked != ENOENT)
            REBOOT_LOG_WARN(Play, "the winhost token file {} could not be removed (errno {})", path_.string(),
                            unlinked);
        const int removed = ::rmdir(directory_.c_str()) == 0 ? 0 : errno;
        if (removed != 0 && removed != ENOENT)
            REBOOT_LOG_WARN(Play, "the winhost token directory {} could not be removed (errno {})",
                            directory_.string(), removed);
    }

    PosixTokenFile(const PosixTokenFile&) = delete;
    PosixTokenFile& operator=(const PosixTokenFile&) = delete;

    [[nodiscard]] const NativePath& path() const override { return path_; }

private:
    NativePath directory_;
    NativePath path_;
    std::size_t size_;
};

}  // namespace

Result<void> write_exclusive_file(const NativePath& path, std::string_view bytes) {
    const UniqueFd fd{::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
    if (!fd.valid()) return std::unexpected(call_failed("open", errno, path));
    // The umask only narrows 0600, but a stricter one would leave the reader locked out too.
    if (::fchmod(fd.get(), 0600) != 0) return std::unexpected(call_failed("fchmod", errno, path));
    std::size_t done = 0;
    while (done < bytes.size()) {
        const ssize_t wrote = ::write(fd.get(), bytes.data() + done, bytes.size() - done);
        if (wrote < 0 && errno == EINTR) continue;
        if (wrote <= 0) return std::unexpected(call_failed("write", wrote < 0 ? errno : EIO, path));
        done += static_cast<std::size_t>(wrote);
    }
    return {};
}

Result<std::unique_ptr<ports::ITokenFile>> TokenFiles::write(std::string_view token) const {
    if (auto made = make_owner_only_directory(base_); !made) return std::unexpected(std::move(made.error()));
    auto base_fd = open_private_base(base_);
    if (!base_fd) return std::unexpected(std::move(base_fd.error()));
    std::error_code error;
    const NativePath canonical_base = fs::canonical(base_, error);
    if (error) return std::unexpected(call_failed("realpath", error.value(), base_));

    std::string pattern = (canonical_base / (std::string(kDirPrefix) + "XXXXXX")).string();
    if (::mkdtemp(pattern.data()) == nullptr) return std::unexpected(call_failed("mkdtemp", errno, canonical_base));
    // From here on the handle removes what exists of the directory and the file.
    auto file = std::make_unique<PosixTokenFile>(NativePath(pattern), token.size());
    if (auto written = write_exclusive_file(file->path(), token); !written)
        return std::unexpected(std::move(written.error()));
    return std::unique_ptr<ports::ITokenFile>(std::move(file));
}

void TokenFiles::sweep() const {
    struct stat info {};
    if (::lstat(base_.c_str(), &info) != 0) return;
    if (auto base_fd = open_private_base(base_); !base_fd) {
        REBOOT_LOG_WARN(Play, "stale winhost token files under {} are left alone: {}", base_.string(),
                        base_fd.error().id);
        return;
    }
    std::vector<NativePath> stale;
    std::error_code error;
    for (fs::directory_iterator it(base_, error), end; !error && it != end; it.increment(error))
        if (it->path().filename().string().starts_with(kDirPrefix)) stale.push_back(it->path());
    for (const NativePath& entry : stale) {
        std::error_code removed;
        // remove_all never follows a link it meets.
        fs::remove_all(entry, removed);
        if (removed) REBOOT_LOG_WARN(Play, "the stale winhost token entry {} could not be removed", entry.string());
    }
}

}  // namespace rb::posix
