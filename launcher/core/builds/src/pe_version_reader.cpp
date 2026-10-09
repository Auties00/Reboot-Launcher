#include "reboot/builds/pe_version_reader.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

#include "pe_error.hpp"
#include "reboot/builds/byte_source.hpp"
#include "release_marker_scan.hpp"

namespace rb::builds {

namespace {

constexpr u64 kDosHeaderSize = 64;
constexpr u64 kLfanewOffset = 0x3C;
constexpr u64 kFileHeaderSize = 20;
constexpr u64 kSectionHeaderSize = 40;
constexpr u16 kPe32Magic = 0x10B;
constexpr u16 kPe32PlusMagic = 0x20B;
constexpr u32 kResourceDirectoryIndex = 2;
constexpr u32 kRtVersion = 16;
constexpr u32 kHighBit = 0x8000'0000u;
constexpr u64 kResourceDirectorySize = 16;
constexpr u64 kResourceEntrySize = 8;
constexpr u64 kResourceDataEntrySize = 16;

constexpr std::size_t kScanChunk = std::size_t{1} << 20;
// Bytes a match may reach back for its "X.Y.Z-<cl>+" prefix and forward for its marker and tail.
constexpr std::size_t kScanLookback = 256;
constexpr std::size_t kScanLookahead = (kReleaseMarkerText.size() + kReleaseTailCap + 1) * 2;

[[nodiscard]] Diagnostic pe_error(PeErrorCode code, u64 offset = 0, std::optional<Diagnostic> cause = std::nullopt) {
    return to_diagnostic(PeError{.code = code, .offset = offset, .cause = std::move(cause)});
}

[[nodiscard]] u16 le16(std::span<const u8> bytes, std::size_t at) noexcept {
    return static_cast<u16>(bytes[at] | (bytes[at + 1] << 8));
}

[[nodiscard]] u32 le32(std::span<const u8> bytes, std::size_t at) noexcept {
    return static_cast<u32>(bytes[at]) | (static_cast<u32>(bytes[at + 1]) << 8) |
           (static_cast<u32>(bytes[at + 2]) << 16) | (static_cast<u32>(bytes[at + 3]) << 24);
}

struct Section {
    u32 virtual_address = 0;
    u32 virtual_size = 0;
    u32 raw_size = 0;
    u32 raw_offset = 0;
};

// Every read is bounds-checked against the source first, so a short file is Malformed, never a read error.
class PeImage {
public:
    explicit PeImage(IByteSource& source) : source_(source) {}

    Result<void> read(u64 offset, std::span<u8> out) {
        if (offset > source_.size() || out.size() > source_.size() - offset)
            return std::unexpected(pe_error(PeErrorCode::Malformed, offset));
        if (auto read = source_.read_at(offset, out); !read)
            return std::unexpected(pe_error(PeErrorCode::ReadFailed, offset, std::move(read.error())));
        return {};
    }

    Result<void> load_headers() {
        if (source_.size() < kDosHeaderSize) return std::unexpected(pe_error(PeErrorCode::NotPe));
        std::array<u8, kDosHeaderSize> dos{};
        if (auto read = this->read(0, dos); !read) return read;
        if (dos[0] != 'M' || dos[1] != 'Z') return std::unexpected(pe_error(PeErrorCode::NotPe));

        const u64 nt = le32(dos, kLfanewOffset);
        std::array<u8, 4 + kFileHeaderSize> header{};
        if (nt > source_.size() || header.size() > source_.size() - nt)
            return std::unexpected(pe_error(PeErrorCode::NotPe));
        if (auto read = this->read(nt, header); !read) return read;
        if (header[0] != 'P' || header[1] != 'E' || header[2] != 0 || header[3] != 0)
            return std::unexpected(pe_error(PeErrorCode::NotPe));

        const u16 section_count = le16(header, 4 + 2);
        const u16 optional_size = le16(header, 4 + 16);
        const u64 optional_at = nt + 4 + kFileHeaderSize;
        std::vector<u8> optional(optional_size);
        if (auto read = this->read(optional_at, optional); !read) return read;
        if (optional.size() < 2) return std::unexpected(pe_error(PeErrorCode::Malformed, optional_at));

        const u16 magic = le16(optional, 0);
        std::size_t count_at = 0;
        if (magic == kPe32Magic) count_at = 92;
        else if (magic == kPe32PlusMagic) count_at = 108;
        else return std::unexpected(pe_error(PeErrorCode::Malformed, optional_at));
        const std::size_t directories_at = count_at + 4;
        if (optional.size() < directories_at) return std::unexpected(pe_error(PeErrorCode::Malformed, optional_at));
        const u32 directory_count = le32(optional, count_at);
        const std::size_t resource_at = directories_at + (kResourceDirectoryIndex * 8);
        if (directory_count > kResourceDirectoryIndex && optional.size() >= resource_at + 8) {
            resource_rva_ = le32(optional, resource_at);
            resource_size_ = le32(optional, resource_at + 4);
        }

        const u64 sections_at = optional_at + optional_size;
        std::vector<u8> table(static_cast<std::size_t>(section_count) * kSectionHeaderSize);
        if (auto read = this->read(sections_at, table); !read) return read;
        sections_.reserve(section_count);
        for (std::size_t i = 0; i < section_count; ++i) {
            const std::size_t at = i * kSectionHeaderSize;
            sections_.push_back(Section{.virtual_address = le32(table, at + 12),
                                        .virtual_size = le32(table, at + 8),
                                        .raw_size = le32(table, at + 16),
                                        .raw_offset = le32(table, at + 20)});
        }
        return {};
    }

    // The file offset of `length` bytes at `rva`, all inside one section's raw data and the file.
    Result<u64> offset_of(u64 rva, u64 length) const {
        for (const Section& section : sections_) {
            if (rva < section.virtual_address) continue;
            const u64 into = rva - section.virtual_address;
            if (into >= std::max(section.virtual_size, section.raw_size)) continue;
            if (into > section.raw_size || length > section.raw_size - into) break;
            const u64 offset = u64{section.raw_offset} + into;
            if (offset > source_.size() || length > source_.size() - offset) break;
            return offset;
        }
        return std::unexpected(pe_error(PeErrorCode::Malformed, rva));
    }

    // Reads `out.size()` bytes `offset` bytes into the resource directory.
    Result<void> read_resource(u64 offset, std::span<u8> out) {
        if (offset > resource_size_ || out.size() > resource_size_ - offset)
            return std::unexpected(pe_error(PeErrorCode::Malformed, u64{resource_rva_} + offset));
        auto at = offset_of(u64{resource_rva_} + offset, out.size());
        if (!at) return std::unexpected(std::move(at.error()));
        return read(*at, out);
    }

    // The data or subdirectory offset of the entry `select` picks in the directory at `offset`.
    template <class Select>
    Result<std::optional<u32>> pick_entry(u64 offset, Select&& select) {
        std::array<u8, kResourceDirectorySize> directory{};
        if (auto read = read_resource(offset, directory); !read) return std::unexpected(std::move(read.error()));
        const u64 count = u64{le16(directory, 12)} + le16(directory, 14);
        std::vector<u8> entries(static_cast<std::size_t>(count * kResourceEntrySize));
        if (auto read = read_resource(offset + kResourceDirectorySize, entries); !read)
            return std::unexpected(std::move(read.error()));
        for (std::size_t i = 0; i < count; ++i) {
            const u32 name = le32(entries, i * kResourceEntrySize);
            const u32 target = le32(entries, (i * kResourceEntrySize) + 4);
            if (select(name, target)) return std::optional<u32>(target);
        }
        return std::optional<u32>();
    }

    [[nodiscard]] u32 resource_rva() const noexcept { return resource_rva_; }

private:
    IByteSource& source_;
    u32 resource_rva_ = 0;
    u32 resource_size_ = 0;
    std::vector<Section> sections_;
};

[[nodiscard]] bool is_subdirectory(u32 target) noexcept { return (target & kHighBit) != 0; }

}  // namespace

Result<std::vector<u8>> PeVersionReader::read_version_resource(IByteSource& source) const {
    PeImage image(source);
    if (auto loaded = image.load_headers(); !loaded) return std::unexpected(std::move(loaded.error()));
    if (image.resource_rva() == 0) return std::unexpected(pe_error(PeErrorCode::NoVersionResource));

    auto types = image.pick_entry(0, [](u32 name, u32 target) {
        return (name & kHighBit) == 0 && name == kRtVersion && is_subdirectory(target);
    });
    if (!types) return std::unexpected(std::move(types.error()));
    if (!*types) return std::unexpected(pe_error(PeErrorCode::NoVersionResource));

    auto names = image.pick_entry(**types & ~kHighBit, [](u32, u32 target) { return is_subdirectory(target); });
    if (!names) return std::unexpected(std::move(names.error()));
    if (!*names) return std::unexpected(pe_error(PeErrorCode::NoVersionResource));

    auto languages = image.pick_entry(**names & ~kHighBit, [](u32, u32 target) { return !is_subdirectory(target); });
    if (!languages) return std::unexpected(std::move(languages.error()));
    if (!*languages) return std::unexpected(pe_error(PeErrorCode::NoVersionResource));

    std::array<u8, kResourceDataEntrySize> data_entry{};
    if (auto read = image.read_resource(**languages, data_entry); !read)
        return std::unexpected(std::move(read.error()));
    const u32 data_rva = le32(data_entry, 0);
    const u32 data_size = le32(data_entry, 4);
    if (data_size > kVersionResourceCap) return std::unexpected(pe_error(PeErrorCode::ResourceTooLarge, data_rva));
    if (data_size == 0) return std::unexpected(pe_error(PeErrorCode::Malformed, data_rva));

    auto offset = image.offset_of(data_rva, data_size);
    if (!offset) return std::unexpected(std::move(offset.error()));
    std::vector<u8> resource(data_size);
    if (auto read = image.read(*offset, resource); !read) return std::unexpected(std::move(read.error()));
    return resource;
}

Result<std::optional<ReleaseMarker>> PeVersionReader::read_marker(IByteSource& source) const {
    auto resource = read_version_resource(source);
    if (!resource) return std::unexpected(std::move(resource.error()));
    return find_release_marker(*resource);
}

Result<std::optional<ReleaseMarker>> PeVersionReader::scan_for_marker(IByteSource& source,
                                                                       const CancelToken& token) const {
    const u64 size = source.size();
    std::vector<u8> window;
    u64 start = 0;
    while (start < size) {
        if (token.cancelled()) return std::unexpected(pe_error(PeErrorCode::Cancelled));
        const u64 end = std::min<u64>(size, start + kScanLookback + kScanChunk + kScanLookahead);
        window.resize(static_cast<std::size_t>(end - start));
        if (auto read = source.read_at(start, window); !read)
            return std::unexpected(pe_error(PeErrorCode::ReadFailed, start, std::move(read.error())));

        // Matches are taken from [first, last) here; the overlap re-reads the bytes around both edges.
        const std::size_t first = start == 0 ? 0 : kScanLookback;
        const bool at_end = end == size;
        const std::size_t last = at_end ? window.size() : window.size() - kScanLookahead;
        if (auto match = find_release_marker_from(window, first); match && match->offset < last)
            return std::optional<ReleaseMarker>(std::move(match->marker));
        if (at_end) break;
        start += last - kScanLookback;
    }
    return std::optional<ReleaseMarker>();
}

}  // namespace rb::builds
