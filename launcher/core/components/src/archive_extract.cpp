#include "archive_extract.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

namespace reboot::components {

namespace {

struct ReadDeleter {
    void operator()(archive* handle) const noexcept { archive_read_free(handle); }
};
struct WriteDeleter {
    void operator()(archive* handle) const noexcept { archive_write_free(handle); }
};

constexpr std::size_t kReadBlock = 64 * 1024;

[[nodiscard]] std::unexpected<ExtractError> failed(archive* handle, std::string_view what) {
    std::string detail(what);
    if (const char* reason = archive_error_string(handle)) {
        detail += ": ";
        detail += reason;
    }
    return std::unexpected(ExtractError{.cancelled = false, .detail = std::move(detail)});
}

// The entry's path below the archive root; nullopt for an absolute path or one with "..".
[[nodiscard]] std::optional<NativePath> inside_path(const NativePath& raw) {
    if (raw.has_root_name() || raw.has_root_directory()) return std::nullopt;
    NativePath out;
    for (const NativePath& part : raw) {
        if (part == "..") return std::nullopt;
        if (part.empty() || part == ".") continue;
        out /= part;
    }
    return out;
}

[[nodiscard]] std::optional<NativePath> entry_path(archive_entry* entry) {
#if defined(_WIN32)
    const wchar_t* path = archive_entry_pathname_w(entry);
#else
    const char* path = archive_entry_pathname(entry);
#endif
    if (path == nullptr) return std::nullopt;
    return inside_path(NativePath(path));
}

[[nodiscard]] std::optional<NativePath> hardlink_path(archive_entry* entry) {
#if defined(_WIN32)
    const wchar_t* path = archive_entry_hardlink_w(entry);
#else
    const char* path = archive_entry_hardlink(entry);
#endif
    if (path == nullptr) return NativePath();
    return inside_path(NativePath(path));
}

void set_paths(archive_entry* entry, const NativePath& path, const std::optional<NativePath>& hardlink) {
#if defined(_WIN32)
    archive_entry_copy_pathname_w(entry, path.c_str());
    if (hardlink) archive_entry_copy_hardlink_w(entry, hardlink->c_str());
#else
    archive_entry_copy_pathname(entry, path.c_str());
    if (hardlink) archive_entry_copy_hardlink(entry, hardlink->c_str());
#endif
}

[[nodiscard]] int open_archive(archive* handle, const NativePath& path) {
#if defined(_WIN32)
    return archive_read_open_filename_w(handle, path.c_str(), kReadBlock);
#else
    return archive_read_open_filename(handle, path.c_str(), kReadBlock);
#endif
}

}  // namespace

std::expected<u64, ExtractError> extract_archive(const NativePath& archive_path, const NativePath& dest,
                                                 const CancelToken& token,
                                                 UniqueFunction<void(u64 archive_bytes_read)> on_progress) {
    // libarchive refuses to write through any symlink on the way, including one in `dest` itself.
    std::error_code error;
    const NativePath root = std::filesystem::canonical(dest, error);
    if (error) return std::unexpected(ExtractError{.cancelled = false, .detail = error.message()});

    const std::unique_ptr<archive, ReadDeleter> in(archive_read_new());
    const std::unique_ptr<archive, WriteDeleter> out(archive_write_disk_new());
    if (!in || !out) return std::unexpected(ExtractError{.cancelled = false, .detail = "out of memory"});
    archive_read_support_filter_all(in.get());
    archive_read_support_format_all(in.get());
    // No owner or extended attributes, so nothing like com.apple.quarantine is restored.
    archive_write_disk_set_options(out.get(), ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM |
                                                  ARCHIVE_EXTRACT_SECURE_SYMLINKS | ARCHIVE_EXTRACT_SECURE_NODOTDOT);
    if (open_archive(in.get(), archive_path) != ARCHIVE_OK) return failed(in.get(), "open");

    u64 unpacked = 0;
    while (true) {
        if (token.cancelled()) return std::unexpected(ExtractError{.cancelled = true, .detail = {}});
        archive_entry* entry = nullptr;
        const int header = archive_read_next_header(in.get(), &entry);
        if (header == ARCHIVE_EOF) break;
        if (header < ARCHIVE_WARN) return failed(in.get(), "read header");

        const std::optional<NativePath> relative = entry_path(entry);
        if (!relative) return std::unexpected(ExtractError{.cancelled = false, .detail = "entry escapes the archive root"});
        if (relative->empty()) continue;
        std::optional<NativePath> hardlink = hardlink_path(entry);
        if (!hardlink) return std::unexpected(ExtractError{.cancelled = false, .detail = "hard link escapes the archive root"});
        set_paths(entry, root / *relative, hardlink->empty() ? std::nullopt : std::optional(root / *hardlink));

        if (archive_write_header(out.get(), entry) < ARCHIVE_WARN) return failed(out.get(), "write header");
        if (archive_entry_size_is_set(entry) == 0 || archive_entry_size(entry) > 0) {
            const void* block = nullptr;
            std::size_t size = 0;
            la_int64_t offset = 0;
            while (true) {
                const int read = archive_read_data_block(in.get(), &block, &size, &offset);
                if (read == ARCHIVE_EOF) break;
                if (read < ARCHIVE_WARN) return failed(in.get(), "read data");
                if (archive_write_data_block(out.get(), block, size, offset) < ARCHIVE_WARN)
                    return failed(out.get(), "write data");
                unpacked += size;
                if (token.cancelled()) return std::unexpected(ExtractError{.cancelled = true, .detail = {}});
            }
        }
        if (archive_write_finish_entry(out.get()) < ARCHIVE_WARN) return failed(out.get(), "finish entry");
        if (on_progress) on_progress(static_cast<u64>(archive_filter_bytes(in.get(), -1)));
    }
    if (archive_write_close(out.get()) != ARCHIVE_OK) return failed(out.get(), "close");
    return unpacked;
}

}  // namespace reboot::components
