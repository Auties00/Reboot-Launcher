#include "reboot/builds/libarchive_extractor.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "builds_error.hpp"
#include "reboot/builds/file_byte_source.hpp"
#include "reboot/foundation/text.hpp"
#include "zip_directory.hpp"

// Last: on Windows it brings in windows.h and its min and max macros.
#include <archive.h>
#include <archive_entry.h>

namespace reboot::builds {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kReadBuffer = std::size_t{1} << 20;

// Device names Windows resolves in any folder and with any extension.
constexpr std::array<std::string_view, 22> kDosDevices{
    "con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4", "com5", "com6", "com7",
    "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};

[[nodiscard]] Diagnostic error(BuildsErrorCode code, const NativePath& path, std::string entry = {},
                               std::optional<SystemError> os_error = std::nullopt) {
    return to_diagnostic(
        BuildsError{.code = code, .path = path, .archive_entry = std::move(entry), .os_error = os_error});
}

// Probe errors carry an empty path, since a byte source has none.
[[nodiscard]] Diagnostic with_path(Diagnostic diag, const NativePath& path) {
    for (auto& [name, value] : diag.args)
        if (name == "path") value = detail::to_arg(path);
    return diag;
}

// \\?\ lifts MAX_PATH for every file the extraction opens on Windows.
[[nodiscard]] NativePath long_path(const NativePath& path) {
#ifdef _WIN32
    const NativePath normal = fs::absolute(path).lexically_normal();
    const std::wstring& text = normal.native();
    if (text.starts_with(LR"(\\?\)")) return normal;
    if (text.starts_with(LR"(\\)")) return NativePath(LR"(\\?\UNC\)" + text.substr(2));
    return NativePath(LR"(\\?\)" + text);
#else
    return path;
#endif
}

// Reads the archive file, or the ZipStored window inside it, with seeking for 7z and seekable ZIP.
struct Input {
    std::ifstream stream;
    u64 base = 0;
    u64 length = 0;
    u64 position = 0;
    std::vector<char> buffer = std::vector<char>(kReadBuffer);
};

la_ssize_t read_input(archive* reader, void* data, const void** out) {
    Input& input = *static_cast<Input*>(data);
    const u64 left = input.length - input.position;
    if (left == 0) return 0;
    const auto wanted = static_cast<std::streamsize>((std::min<u64>)(left, input.buffer.size()));
    input.stream.clear();
    input.stream.seekg(static_cast<std::streamoff>(input.base + input.position));
    input.stream.read(input.buffer.data(), wanted);
    const std::streamsize got = input.stream.gcount();
    if (got <= 0) {
        archive_set_error(reader, EIO, "cannot read the archive file");
        return ARCHIVE_FATAL;
    }
    input.position += static_cast<u64>(got);
    *out = input.buffer.data();
    return got;
}

la_int64_t seek_input(archive*, void* data, la_int64_t offset, int whence) {
    Input& input = *static_cast<Input*>(data);
    i64 target = offset;
    if (whence == SEEK_CUR) target += static_cast<i64>(input.position);
    else if (whence == SEEK_END) target += static_cast<i64>(input.length);
    if (target < 0) return ARCHIVE_FATAL;
    input.position = (std::min<u64>)(static_cast<u64>(target), input.length);
    return static_cast<la_int64_t>(input.position);
}

la_int64_t skip_input(archive*, void* data, la_int64_t request) {
    Input& input = *static_cast<Input*>(data);
    if (request <= 0) return 0;
    const u64 skipped = (std::min<u64>)(static_cast<u64>(request), input.length - input.position);
    input.position += skipped;
    return static_cast<la_int64_t>(skipped);
}

struct ReaderDeleter {
    void operator()(archive* reader) const noexcept { archive_read_free(reader); }
};
using Reader = std::unique_ptr<archive, ReaderDeleter>;

[[nodiscard]] std::string entry_name(archive_entry* entry) {
#ifdef _WIN32
    if (const wchar_t* wide = archive_entry_pathname_w(entry))
        return utf16_to_utf8(std::u16string_view(reinterpret_cast<const char16_t*>(wide), std::wcslen(wide)));
#endif
    if (const char* utf8 = archive_entry_pathname_utf8(entry)) return utf8;
    if (const char* raw = archive_entry_pathname(entry)) return raw;
    return {};
}

[[nodiscard]] std::string lowered(std::string_view text) {
    std::string out(text);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

[[nodiscard]] bool is_dos_device(std::string_view component) {
    std::string base = lowered(component.substr(0, component.find('.')));
    while (!base.empty() && base.back() == ' ') base.pop_back();
    return std::ranges::find(kDosDevices, base) != kDosDevices.end();
}

// The entry's folder components, '\' read as '/', or nullopt when it would leave the destination.
[[nodiscard]] std::optional<std::vector<std::string>> safe_components(std::string name) {
    std::ranges::replace(name, '\\', '/');
    if (name.starts_with('/')) return std::nullopt;
    std::vector<std::string> parts;
    std::size_t at = 0;
    while (at <= name.size()) {
        const std::size_t end = (std::min)(name.find('/', at), name.size());
        std::string part = name.substr(at, end - at);
        at = end + 1;
        if (part.empty() || part == ".") continue;
        if (part == ".." || part.find(':') != std::string::npos || is_dos_device(part)) return std::nullopt;
        if (std::ranges::any_of(part, [](char c) { return static_cast<unsigned char>(c) < 0x20; }))
            return std::nullopt;
        parts.push_back(std::move(part));
    }
    return parts;
}

// Maps each entry onto the casing first seen for its folded path, as a case-insensitive volume would.
class CaseFolder {
public:
    // The on-disk relative path, '/'-separated.
    std::string place(const std::vector<std::string>& parts, bool is_file, std::vector<CaseCollision>& collisions) {
        std::string actual;
        std::string folded;
        std::string original;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                actual += '/';
                folded += '/';
                original += '/';
            }
            folded += lowered(parts[i]);
            original += parts[i];
            const auto [it, inserted] = seen_.try_emplace(folded, actual + parts[i]);
            actual = it->second;
        }
        // Compared by the names in the archive, since a remapped folder can make two written paths equal.
        if (is_file) {
            const auto [it, inserted] = files_.try_emplace(folded, original);
            if (!inserted && it->second != original) {
                collisions.push_back(CaseCollision{.kept = original, .replaced = it->second});
                it->second = original;
            }
        }
        return actual;
    }

private:
    std::unordered_map<std::string, std::string> seen_;
    // The archive name of the file last written at each folded path.
    std::unordered_map<std::string, std::string> files_;
};

[[nodiscard]] NativePath relative_path(std::string_view utf8) {
    return NativePath(std::u8string(utf8.begin(), utf8.end())).make_preferred();
}

class Extraction {
public:
    Extraction(const ExtractRequest& request, const CancelToken& token,
               UniqueFunction<void(const ExtractProgress&)>& on_progress)
        : request_(request), token_(token), on_progress_(on_progress), destination_(long_path(request.destination)) {}

    Result<ExtractSummary> run() {
        Result<FileByteSource> source = FileByteSource::open(request_.archive);
        if (!source) return std::unexpected(std::move(source.error()));
        Result<ArchiveProbe> probe = probe_archive(*source);
        if (!probe) return std::unexpected(with_path(std::move(probe.error()), request_.archive));
        summary_.probe = *probe;
        if (probe->format == catalog::ArchiveFormat::Zip) {
            if (auto entries = read_zip_directory(*source)) {
                u64 total = 0;
                for (const ZipEntry& entry : *entries) total += entry.uncompressed_size;
                progress_.bytes_total = total;
            }
        }

        input_.stream.open(long_path(request_.archive), std::ios::binary);
        if (!input_.stream) return std::unexpected(error(BuildsErrorCode::Io, request_.archive));
        input_.base = probe->window ? probe->window->offset : 0;
        input_.length = probe->window ? probe->window->length : source->size();

        Reader reader(archive_read_new());
        if (!reader) return std::unexpected(error(BuildsErrorCode::Io, request_.archive));
        switch (probe->format) {
            case catalog::ArchiveFormat::Zip: archive_read_support_format_zip_seekable(reader.get()); break;
            case catalog::ArchiveFormat::SevenZip: archive_read_support_format_7zip(reader.get()); break;
            case catalog::ArchiveFormat::Rar:
                archive_read_support_format_rar(reader.get());
                archive_read_support_format_rar5(reader.get());
                break;
            case catalog::ArchiveFormat::Unrecognized:
                return std::unexpected(error(BuildsErrorCode::UnsupportedArchive, request_.archive));
        }
        archive_read_set_read_callback(reader.get(), read_input);
        archive_read_set_seek_callback(reader.get(), seek_input);
        archive_read_set_skip_callback(reader.get(), skip_input);
        archive_read_set_callback_data(reader.get(), &input_);
        if (archive_read_open1(reader.get()) != ARCHIVE_OK) return std::unexpected(read_failure(reader.get(), {}));

        while (true) {
            if (token_.cancelled()) return std::unexpected(error(BuildsErrorCode::Cancelled, {}));
            archive_entry* entry = nullptr;
            const int status = archive_read_next_header(reader.get(), &entry);
            if (status == ARCHIVE_EOF) break;
            if (status < ARCHIVE_WARN) return std::unexpected(read_failure(reader.get(), {}));
            if (auto written = write_entry(reader.get(), entry); !written)
                return std::unexpected(std::move(written.error()));
            ++progress_.entries_done;
            on_progress_(progress_);
        }
        summary_.entries = progress_.entries_done;
        summary_.bytes = progress_.bytes_done;
        return std::move(summary_);
    }

private:
    [[nodiscard]] Diagnostic read_failure(archive* reader, const std::string& entry) const {
        const char* text = archive_error_string(reader);
        const std::string message = text != nullptr ? text : "";
        const std::string lower = lowered(message);
        const bool unsupported = lower.find("unsupported") != std::string::npos ||
                                 lower.find("encrypt") != std::string::npos ||
                                 lower.find("not supported") != std::string::npos;
        Diagnostic diag = unsupported ? error(BuildsErrorCode::UnsupportedArchive, request_.archive)
                                      : error(BuildsErrorCode::CorruptArchive, request_.archive, entry);
        if (!message.empty()) diag.detail = normalize_detail(message);
        return diag;
    }

    Result<void> write_entry(archive* reader, archive_entry* entry) {
        const std::string name = entry_name(entry);
        if (archive_entry_is_encrypted(entry) != 0)
            return std::unexpected(error(BuildsErrorCode::UnsupportedArchive, request_.archive, name));
        const auto type = archive_entry_filetype(entry);
        const bool is_dir = type == AE_IFDIR;
        if ((!is_dir && type != AE_IFREG) || archive_entry_hardlink(entry) != nullptr)
            return std::unexpected(error(BuildsErrorCode::UnsafeEntryPath, request_.archive, name));
        const std::optional<std::vector<std::string>> parts = safe_components(name);
        if (!parts) return std::unexpected(error(BuildsErrorCode::UnsafeEntryPath, request_.archive, name));
        if (parts->empty()) return {};

        const NativePath target =
            destination_ / relative_path(folder_.place(*parts, !is_dir, summary_.case_collisions));
        std::error_code ec;
        fs::create_directories(is_dir ? target : target.parent_path(), ec);
        if (ec) return std::unexpected(error(BuildsErrorCode::Io, target, {}, SystemError{.code = ec.value()}));
        if (is_dir) return {};

        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) return std::unexpected(error(BuildsErrorCode::Io, target));
        u64 written = 0;
        while (true) {
            if (token_.cancelled()) return std::unexpected(error(BuildsErrorCode::Cancelled, {}));
            const void* block = nullptr;
            std::size_t size = 0;
            la_int64_t offset = 0;
            const int status = archive_read_data_block(reader, &block, &size, &offset);
            if (status == ARCHIVE_EOF) break;
            if (status < ARCHIVE_WARN) return std::unexpected(read_failure(reader, name));
            if (static_cast<u64>(offset) != written) out.seekp(static_cast<std::streamoff>(offset));
            out.write(static_cast<const char*>(block), static_cast<std::streamsize>(size));
            if (!out) return std::unexpected(error(BuildsErrorCode::Io, target));
            written = static_cast<u64>(offset) + size;
            progress_.bytes_done += size;
            on_progress_(progress_);
        }
        out.close();
        if (!out) return std::unexpected(error(BuildsErrorCode::Io, target));
        // A sparse entry may end in a hole the data blocks never reach.
        if (archive_entry_size_is_set(entry) != 0 && static_cast<u64>(archive_entry_size(entry)) > written) {
            fs::resize_file(target, static_cast<std::uintmax_t>(archive_entry_size(entry)), ec);
            if (ec) return std::unexpected(error(BuildsErrorCode::Io, target, {}, SystemError{.code = ec.value()}));
        }
        return {};
    }

    const ExtractRequest& request_;
    const CancelToken& token_;
    UniqueFunction<void(const ExtractProgress&)>& on_progress_;
    NativePath destination_;
    Input input_;
    CaseFolder folder_;
    ExtractProgress progress_;
    ExtractSummary summary_;
};

}  // namespace

Result<ExtractSummary> LibArchiveExtractor::extract(const ExtractRequest& request, const CancelToken& token,
                                                    UniqueFunction<void(const ExtractProgress&)> on_progress) {
    Extraction extraction(request, token, on_progress);
    return extraction.run();
}

}  // namespace reboot::builds
