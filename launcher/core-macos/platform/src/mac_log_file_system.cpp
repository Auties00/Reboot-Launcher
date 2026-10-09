#include "darwin.hpp"

#include "mac_log_file_system.hpp"

#include <cerrno>
#include <chrono>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <memory>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
#include <utility>

#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/unique_fd.hpp"

namespace reboot::os_macos::platform {

namespace {

[[nodiscard]] std::chrono::system_clock::time_point modified_time(const struct stat& info) noexcept {
    return std::chrono::system_clock::time_point{std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::seconds{info.st_mtimespec.tv_sec} + std::chrono::nanoseconds{info.st_mtimespec.tv_nsec})};
}

class AppendFile final : public ports::LogFile::Handle {
public:
    AppendFile(NativePath path, posix::UniqueFd fd) : path_(std::move(path)), fd_(std::move(fd)) {}

    Result<void> append(std::span<const u8> bytes) override {
        while (!bytes.empty()) {
            const ssize_t written = ::write(fd_.get(), bytes.data(), bytes.size());
            if (written < 0) {
                if (errno == EINTR) continue;
                return std::unexpected(posix::call_failed("write", errno, path_));
            }
            bytes = bytes.subspan(static_cast<std::size_t>(written));
        }
        return {};
    }

    Result<void> flush() override {
        while (::fsync(fd_.get()) != 0) {
            if (errno != EINTR) return std::unexpected(posix::call_failed("fsync", errno, path_));
        }
        return {};
    }

private:
    NativePath path_;
    posix::UniqueFd fd_;
};

struct DirCloser {
    void operator()(DIR* listing) const noexcept { ::closedir(listing); }
};

}  // namespace

Result<void> MacLogFileSystem::create_directories(const NativePath& dir) {
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error) return std::unexpected(posix::call_failed("mkdir", error.value(), dir));
    return {};
}

Result<ports::LogFile> MacLogFileSystem::open_append(const NativePath& path) {
    posix::UniqueFd fd{::open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600)};
    if (!fd.valid()) return std::unexpected(posix::call_failed("open", errno, path));
    struct stat info {};
    if (::fstat(fd.get(), &info) != 0) return std::unexpected(posix::call_failed("fstat", errno, path));
    if (!S_ISREG(info.st_mode)) return std::unexpected(posix::call_failed("open", EINVAL, path));
    const auto size = static_cast<u64>(info.st_size);
    return ports::LogFile{std::make_unique<AppendFile>(path, std::move(fd)), size};
}

Result<std::vector<ports::LogDirEntry>> MacLogFileSystem::list(const NativePath& dir) {
    posix::UniqueFd directory{::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    if (!directory.valid()) return std::unexpected(posix::call_failed("open", errno, dir));
    std::unique_ptr<DIR, DirCloser> listing{::fdopendir(directory.get())};
    if (!listing) return std::unexpected(posix::call_failed("fdopendir", errno, dir));
    // The DIR now owns the descriptor.
    const int directory_fd = directory.release();

    std::vector<ports::LogDirEntry> entries;
    for (;;) {
        errno = 0;
        const dirent* entry = ::readdir(listing.get());
        if (entry == nullptr) {
            if (errno != 0) return std::unexpected(posix::call_failed("readdir", errno, dir));
            break;
        }
        const std::string_view name{entry->d_name};
        if (name == "." || name == "..") continue;
        struct stat info {};
        if (::fstatat(directory_fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno == ENOENT) continue;
            return std::unexpected(posix::call_failed("fstatat", errno, dir / name));
        }
        if (!S_ISREG(info.st_mode)) continue;
        entries.push_back(
            {.path = dir / name, .size = static_cast<u64>(info.st_size), .modified = modified_time(info)});
    }
    return entries;
}

Result<void> MacLogFileSystem::remove(const NativePath& path) {
    if (::unlink(path.c_str()) != 0) return std::unexpected(posix::call_failed("unlink", errno, path));
    return {};
}

}  // namespace reboot::os_macos::platform
