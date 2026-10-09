#include "reboot/posix/posix_file_system.hpp"

#include <cerrno>
#include <chrono>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>

#include "messages.hpp"
#include "owner_only_directory.hpp"
#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/unique_fd.hpp"
#include "unistd.hpp"

namespace reboot::posix {

namespace {

constexpr std::size_t kReadGrowth = 64 * 1024;

[[nodiscard]] std::chrono::system_clock::time_point modified_time(const struct stat& info) noexcept {
#if defined(__APPLE__)
    const timespec& time = info.st_mtimespec;
#else
    const timespec& time = info.st_mtim;
#endif
    return std::chrono::system_clock::time_point{std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::seconds{time.tv_sec} + std::chrono::nanoseconds{time.tv_nsec})};
}

[[nodiscard]] Result<void> write_all(int fd, std::span<const u8> bytes, const NativePath& path) {
    while (!bytes.empty()) {
        const auto written = ::write(fd, bytes.data(), bytes.size());
        if (written < 0) {
            if (errno == EINTR) continue;
            return std::unexpected(call_failed("write", errno, path));
        }
        bytes = bytes.subspan(static_cast<std::size_t>(written));
    }
    return {};
}

// A trailing "/" or "/." makes lstat and O_NOFOLLOW resolve a final link, so it is dropped.
[[nodiscard]] NativePath without_trailing_separator(NativePath path) {
    while (path.has_relative_path() && (!path.has_filename() || path.filename() == ".") &&
           !path.parent_path().empty())
        path = path.parent_path();
    return path;
}

[[nodiscard]] Diagnostic held_file_changed(const NativePath& path) {
    return make_diag(ErrorDomain::Posix, kHeldFileChanged).arg("path", path);
}

// Closing the fd releases the flock.
class FlockHandle final : public ports::FileLock::Handle {
public:
    explicit FlockHandle(UniqueFd fd) noexcept : fd_(std::move(fd)) {}

private:
    UniqueFd fd_;
};

class HeldReadOnlyFile final : public ports::HeldFile::Handle {
public:
    HeldReadOnlyFile(NativePath path, UniqueFd fd, const struct stat& opened)
        : path_(std::move(path)), fd_(std::move(fd)), opened_(opened) {}

    Result<std::size_t> read(std::span<u8> out) override {
        struct stat at_path {};
        if (::stat(path_.c_str(), &at_path) != 0) {
            if (errno == ENOENT) return std::unexpected(held_file_changed(path_));
            return std::unexpected(call_failed("stat", errno, path_));
        }
        struct stat held {};
        if (::fstat(fd_.get(), &held) != 0) return std::unexpected(call_failed("fstat", errno, path_));
        if (at_path.st_dev != opened_.st_dev || at_path.st_ino != opened_.st_ino ||
            held.st_size != opened_.st_size || modified_time(held) != modified_time(opened_))
            return std::unexpected(held_file_changed(path_));
        for (;;) {
            const auto got = ::read(fd_.get(), out.data(), out.size());
            if (got >= 0) return static_cast<std::size_t>(got);
            if (errno != EINTR) return std::unexpected(call_failed("read", errno, path_));
        }
    }

private:
    NativePath path_;
    UniqueFd fd_;
    struct stat opened_;
};

struct DirCloser {
    void operator()(DIR* listing) const noexcept { ::closedir(listing); }
};

// Lists the whole directory before removing anything, since removing entries mid-readdir can
// skip others on APFS.
[[nodiscard]] Result<void> remove_directory_contents(UniqueFd directory, const NativePath& shown) {
    std::unique_ptr<DIR, DirCloser> listing{::fdopendir(directory.get())};
    if (!listing) return std::unexpected(call_failed("fdopendir", errno, shown));
    const int directory_fd = directory.release();

    std::vector<std::string> names;
    for (;;) {
        errno = 0;
        const dirent* entry = ::readdir(listing.get());
        if (entry == nullptr) {
            if (errno != 0) return std::unexpected(call_failed("readdir", errno, shown));
            break;
        }
        const std::string_view name{entry->d_name};
        if (name != "." && name != "..") names.emplace_back(name);
    }

    for (const std::string& name : names) {
        const NativePath child = shown / name;
        struct stat info {};
        if (::fstatat(directory_fd, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno == ENOENT) continue;
            return std::unexpected(call_failed("fstatat", errno, child));
        }
        int unlink_flags = 0;
        if (S_ISDIR(info.st_mode)) {
            UniqueFd sub{::openat(directory_fd, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
            if (!sub.valid()) return std::unexpected(call_failed("openat", errno, child));
            if (auto removed = remove_directory_contents(std::move(sub), child); !removed) return removed;
            unlink_flags = AT_REMOVEDIR;
        }
        if (::unlinkat(directory_fd, name.c_str(), unlink_flags) != 0 && errno != ENOENT)
            return std::unexpected(call_failed("unlinkat", errno, child));
    }
    return {};
}

}  // namespace

Result<void> PosixFileSystem::atomic_replace(const NativePath& target, std::span<const u8> bytes, bool keep_backup) {
    if (!sync_file_ || !sync_directory_) return std::unexpected(internal_bug("posix.atomic_replace_hooks"));

    std::string temp_name = target.native() + ".XXXXXX";
    UniqueFd file{::mkostemp(temp_name.data(), O_CLOEXEC)};
    if (!file.valid()) return std::unexpected(call_failed("mkostemp", errno, target));
    const NativePath temp{temp_name};

    auto staged = [&]() -> Result<void> {
        if (auto written = write_all(file.get(), bytes, temp); !written) return written;
        if (auto synced = sync_file_(file.get()); !synced) return synced;
        // close can report a deferred write error, on NFS for instance.
        if (::close(file.release()) != 0) return std::unexpected(call_failed("close", errno, temp));
        if (keep_backup) {
            const NativePath backup = NativePath{target.native() + ".bak"};
            struct stat previous {};
            // Without a previous file an older backup may be the only copy left, so it stays.
            if (::lstat(target.c_str(), &previous) == 0) {
                if (::unlink(backup.c_str()) != 0 && errno != ENOENT)
                    return std::unexpected(call_failed("unlink", errno, backup));
                if (::linkat(AT_FDCWD, target.c_str(), AT_FDCWD, backup.c_str(), 0) != 0 && errno != ENOENT)
                    return std::unexpected(call_failed("linkat", errno, backup));
            } else if (errno != ENOENT) {
                return std::unexpected(call_failed("lstat", errno, target));
            }
        }
        if (::rename(temp.c_str(), target.c_str()) != 0) return std::unexpected(call_failed("rename", errno, target));
        return {};
    }();
    if (!staged) {
        ::unlink(temp.c_str());
        return staged;
    }

    NativePath directory = target.parent_path();
    if (directory.empty()) directory = ".";
    const UniqueFd directory_fd{::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    if (!directory_fd.valid()) return std::unexpected(call_failed("open", errno, directory));
    return sync_directory_(directory_fd.get());
}

Result<std::vector<u8>> PosixFileSystem::read_all(const NativePath& path) {
    const UniqueFd fd{::open(path.c_str(), O_RDONLY | O_CLOEXEC)};
    if (!fd.valid()) return std::unexpected(call_failed("open", errno, path));
    struct stat info {};
    if (::fstat(fd.get(), &info) != 0) return std::unexpected(call_failed("fstat", errno, path));

    std::vector<u8> bytes(static_cast<std::size_t>(info.st_size));
    std::size_t filled = 0;
    for (;;) {
        // The size is a hint: the file may grow, and procfs files report 0.
        if (filled == bytes.size()) bytes.resize(bytes.size() + kReadGrowth);
        const auto got = ::read(fd.get(), bytes.data() + filled, bytes.size() - filled);
        if (got < 0) {
            if (errno == EINTR) continue;
            return std::unexpected(call_failed("read", errno, path));
        }
        if (got == 0) break;
        filled += static_cast<std::size_t>(got);
    }
    bytes.resize(filled);
    return bytes;
}

Result<ports::FileLock> PosixFileSystem::lock_exclusive(const NativePath& path, bool wait) {
    UniqueFd fd{::open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600)};
    if (!fd.valid()) return std::unexpected(call_failed("open", errno, path));
    while (::flock(fd.get(), LOCK_EX | (wait ? 0 : LOCK_NB)) != 0) {
        if (errno == EINTR) continue;
        if (errno == EWOULDBLOCK)
            return make_diag(ErrorDomain::Posix, kLockBusy).arg("path", path).kind(ErrorKind::Conflict).fail();
        return std::unexpected(call_failed("flock", errno, path));
    }
    return ports::FileLock{std::make_unique<FlockHandle>(std::move(fd))};
}

Result<void> PosixFileSystem::restrict_to_owner(const NativePath& given) {
    const NativePath path = without_trailing_separator(given);
    struct stat info {};
    if (::lstat(path.c_str(), &info) != 0) return std::unexpected(call_failed("lstat", errno, path));
    // ELOOP, as O_NOFOLLOW reports a link.
    if (S_ISLNK(info.st_mode)) return std::unexpected(call_failed("chmod", ELOOP, path));
    // Only the owner can swap a link in between lstat and chmod, since the data root is 0700.
    if (::chmod(path.c_str(), S_ISDIR(info.st_mode) ? 0700 : 0600) != 0)
        return std::unexpected(call_failed("chmod", errno, path));
    return {};
}

Result<ports::HeldFile> PosixFileSystem::open_deny_write(const NativePath& path) {
    UniqueFd fd{::open(path.c_str(), O_RDONLY | O_CLOEXEC)};
    if (!fd.valid()) return std::unexpected(call_failed("open", errno, path));
    struct stat opened {};
    if (::fstat(fd.get(), &opened) != 0) return std::unexpected(call_failed("fstat", errno, path));
    return ports::HeldFile{std::make_unique<HeldReadOnlyFile>(path, std::move(fd), opened)};
}

Result<ports::FileRevision> PosixFileSystem::revision(const NativePath& path) {
    return PosixFileRevisionReader{}.revision(path);
}

Result<ports::SharedRead> PosixFileSystem::read_shared(const NativePath& path, u64 offset, std::size_t max_bytes) {
    const UniqueFd fd{::open(path.c_str(), O_RDONLY | O_CLOEXEC)};
    if (!fd.valid()) return std::unexpected(call_failed("open", errno, path));
    struct stat info {};
    if (::fstat(fd.get(), &info) != 0) return std::unexpected(call_failed("fstat", errno, path));
    ports::SharedRead read;
    read.revision = ports::FileRevision{.size = static_cast<u64>(info.st_size),
                                        .mtime = modified_time(info),
                                        .file_id = static_cast<u64>(info.st_ino)};
    if (offset >= read.revision.size) return read;
    read.bytes.resize(max_bytes);
    std::size_t filled = 0;
    while (filled < max_bytes) {
        const auto got = ::pread(fd.get(), read.bytes.data() + filled, max_bytes - filled,
                                 static_cast<off_t>(offset + filled));
        if (got < 0) {
            if (errno == EINTR) continue;
            return std::unexpected(call_failed("pread", errno, path));
        }
        if (got == 0) break;
        filled += static_cast<std::size_t>(got);
    }
    read.bytes.resize(filled);
    return read;
}

Result<void> PosixFileSystem::create_dirs_owner_only(const NativePath& path) {
    NativePath current;
    for (const NativePath& part : path) {
        current /= part;
        struct stat info {};
        // stat, not lstat: an existing ancestor may be a link, such as /var on macOS.
        if (::stat(current.c_str(), &info) == 0) {
            if (!S_ISDIR(info.st_mode)) return std::unexpected(call_failed("mkdir", ENOTDIR, current));
            continue;
        }
        if (errno != ENOENT) return std::unexpected(call_failed("stat", errno, current));
        auto made = make_owner_only_directory(current);
        if (!made) return std::unexpected(std::move(made.error()));
        if (*made) continue;
        // Another process created the path meanwhile; it must still be a directory.
        if (::stat(current.c_str(), &info) != 0) return std::unexpected(call_failed("stat", errno, current));
        if (!S_ISDIR(info.st_mode)) return std::unexpected(call_failed("mkdir", ENOTDIR, current));
    }
    return {};
}

Result<void> PosixFileSystem::remove_tree(const NativePath& given) {
    const NativePath path = without_trailing_separator(given);
    struct stat info {};
    if (::lstat(path.c_str(), &info) != 0) {
        if (errno == ENOENT) return {};
        return std::unexpected(call_failed("lstat", errno, path));
    }
    if (!S_ISDIR(info.st_mode)) {
        if (::unlink(path.c_str()) != 0 && errno != ENOENT) return std::unexpected(call_failed("unlink", errno, path));
        return {};
    }
    UniqueFd directory{::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    if (!directory.valid()) return std::unexpected(call_failed("open", errno, path));
    if (auto removed = remove_directory_contents(std::move(directory), path); !removed) return removed;
    if (::rmdir(path.c_str()) != 0 && errno != ENOENT) return std::unexpected(call_failed("rmdir", errno, path));
    return {};
}

Result<ports::FileRevision> PosixFileRevisionReader::revision(const NativePath& path) {
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0) return std::unexpected(call_failed("stat", errno, path));
    return ports::FileRevision{.size = static_cast<u64>(info.st_size),
                               .mtime = modified_time(info),
                               .file_id = static_cast<u64>(info.st_ino)};
}

}  // namespace reboot::posix
