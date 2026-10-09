#include "update_package.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <cerrno>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
#include <utility>

#include "messages.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/posix/posix_error.hpp"
#include "update_entries.hpp"

namespace reboot::os_linux::platform {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kBlockSize = 64 * 1024;

struct ReadCloser {
    void operator()(archive* handle) const noexcept { ::archive_read_free(handle); }
};
struct WriteCloser {
    void operator()(archive* handle) const noexcept { ::archive_write_free(handle); }
};

[[nodiscard]] Diagnostic archive_failed(std::string_view call, archive* handle) {
    const char* const text = ::archive_error_string(handle);
    return make_diag(ErrorDomain::Platform, kCallFailed)
        .arg("call", call)
        .detail(text != nullptr ? text : "")
        .os(posix::errno_error(::archive_errno(handle)));
}

[[nodiscard]] Diagnostic unsafe(const NativePath& package, std::string_view entry) {
    return make_diag(ErrorDomain::Platform, kUpdateEntryUnsafe).arg("path", package).arg("entry", entry);
}

[[nodiscard]] Diagnostic invalid(const NativePath& package) {
    return make_diag(ErrorDomain::Platform, kUpdatePackageInvalid).arg("path", package);
}

// Writes every entry under `staging`; returns the top-level names it saw.
[[nodiscard]] Result<std::set<std::string>> extract(const NativePath& package, const NativePath& staging) {
    const std::unique_ptr<archive, ReadCloser> reader{::archive_read_new()};
    const std::unique_ptr<archive, WriteCloser> writer{::archive_write_disk_new()};
    if (!reader || !writer) return std::unexpected(posix::call_failed("archive_read_new", ENOMEM));
    ::archive_read_support_filter_zstd(reader.get());
    ::archive_read_support_format_tar(reader.get());
    // Entries are written at absolute paths under `staging`, so libarchive's own symlink and
    // absolute-path guards (which would check every ancestor of `staging` too) are replaced by
    // the containment check below; UNLINK replaces an existing link instead of writing through it.
    ::archive_write_disk_set_options(writer.get(), ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_TIME |
                                                       ARCHIVE_EXTRACT_SECURE_NODOTDOT | ARCHIVE_EXTRACT_UNLINK);
    if (::archive_read_open_filename(reader.get(), package.c_str(), kBlockSize) != ARCHIVE_OK)
        return std::unexpected(archive_failed("archive_read_open_filename", reader.get()));
    std::error_code error;
    const NativePath root = fs::canonical(staging, error);
    if (error) return std::unexpected(posix::call_failed("realpath", error.value(), staging));
    // A link extracted earlier may lead a later entry out of `staging`.
    const auto contained = [&](const std::string& path) {
        std::error_code resolve_error;
        const NativePath parent = fs::weakly_canonical((staging / path).parent_path(), resolve_error);
        return !resolve_error && is_inside(parent, root);
    };

    std::set<std::string> tops;
    for (;;) {
        archive_entry* entry = nullptr;
        const int read = ::archive_read_next_header(reader.get(), &entry);
        if (read == ARCHIVE_EOF) break;
        if (read < ARCHIVE_WARN) return std::unexpected(archive_failed("archive_read_next_header", reader.get()));

        const char* const raw_path = ::archive_entry_pathname(entry);
        const std::string shown = raw_path != nullptr ? raw_path : "";
        const std::optional<std::string> path = safe_entry_path(shown);
        if (!path) return std::unexpected(unsafe(package, shown));
        if (path->empty()) continue;

        const char* const hardlink = ::archive_entry_hardlink(entry);
        const auto type = ::archive_entry_filetype(entry);
        if (hardlink != nullptr) {
            const std::optional<std::string> linked = safe_entry_path(hardlink);
            if (!linked || linked->empty() || !contained(*linked)) return std::unexpected(unsafe(package, shown));
            ::archive_entry_set_hardlink(entry, (staging / *linked).c_str());
        } else if (type == S_IFLNK) {
            const char* const target = ::archive_entry_symlink(entry);
            if (target == nullptr || !link_stays_inside(*path, target)) return std::unexpected(unsafe(package, shown));
        } else if (type != S_IFREG && type != S_IFDIR) {
            return std::unexpected(unsafe(package, shown));
        }
        if (!contained(*path)) return std::unexpected(unsafe(package, shown));
        tops.emplace(top_component(*path));
        ::archive_entry_set_pathname(entry, (staging / *path).c_str());
        ::archive_entry_set_perm(entry, ::archive_entry_perm(entry) & 0777);

        if (::archive_write_header(writer.get(), entry) < ARCHIVE_WARN)
            return std::unexpected(archive_failed("archive_write_header", writer.get()));
        for (;;) {
            const void* block = nullptr;
            std::size_t size = 0;
            la_int64_t offset = 0;
            const int data = ::archive_read_data_block(reader.get(), &block, &size, &offset);
            if (data == ARCHIVE_EOF) break;
            if (data < ARCHIVE_WARN) return std::unexpected(archive_failed("archive_read_data_block", reader.get()));
            if (::archive_write_data_block(writer.get(), block, size, offset) < ARCHIVE_WARN)
                return std::unexpected(archive_failed("archive_write_data_block", writer.get()));
        }
        if (::archive_write_finish_entry(writer.get()) < ARCHIVE_WARN)
            return std::unexpected(archive_failed("archive_write_finish_entry", writer.get()));
    }
    if (::archive_write_close(writer.get()) != ARCHIVE_OK)
        return std::unexpected(archive_failed("archive_write_close", writer.get()));
    return tops;
}

// Each link alone was checked lexically; a chain of them can still escape, so every one is
// resolved again now that all are on disk.
[[nodiscard]] Result<void> check_links(const NativePath& package, const NativePath& staging) {
    std::error_code error;
    const NativePath root = fs::canonical(staging, error);
    if (error) return std::unexpected(posix::call_failed("realpath", error.value(), staging));
    fs::recursive_directory_iterator it{staging, fs::directory_options::none, error};
    for (const fs::recursive_directory_iterator end; !error && it != end; it.increment(error)) {
        if (!it->is_symlink(error)) continue;
        const NativePath target = fs::read_symlink(it->path(), error);
        // Resolved from the link's directory, since weakly_canonical of a dangling link would
        // keep its name rather than follow it.
        const NativePath resolved = error ? NativePath{} : fs::weakly_canonical(it->path().parent_path() / target, error);
        if (error || !is_inside(resolved, root))
            return std::unexpected(unsafe(package, it->path().lexically_relative(staging).native()));
    }
    if (error) return std::unexpected(posix::call_failed("readdir", error.value(), staging));
    return {};
}

}  // namespace

Result<std::string> extract_update(const NativePath& package, const NativePath& staging) {
    Result<std::set<std::string>> tops = extract(package, staging);
    if (!tops) return std::unexpected(std::move(tops.error()));
    if (tops->size() != 1) return std::unexpected(invalid(package));
    const std::string version = *tops->begin();
    struct stat info {};
    if (::lstat((staging / version).c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
        return std::unexpected(invalid(package));
    if (auto contained = check_links(package, staging); !contained) return std::unexpected(std::move(contained.error()));
    return version;
}

}  // namespace reboot::os_linux::platform
