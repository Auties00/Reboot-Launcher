#include "reboot/builds/archive_probe.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "builds_error.hpp"
#include "reboot/builds/byte_source.hpp"
#include "zip_directory.hpp"

namespace rb::builds {

namespace {

constexpr std::array<u8, 4> kZipLocal{'P', 'K', 3, 4};
constexpr std::array<u8, 4> kZipEmpty{'P', 'K', 5, 6};
constexpr std::array<u8, 7> kRar4{'R', 'a', 'r', '!', 0x1A, 0x07, 0x00};
constexpr std::array<u8, 8> kRar5{'R', 'a', 'r', '!', 0x1A, 0x07, 0x01, 0x00};
constexpr std::array<u8, 6> kSevenZip{'7', 'z', 0xBC, 0xAF, 0x27, 0x1C};

constexpr u32 kEocdSignature = 0x06054B50;
constexpr u32 kZip64LocatorSignature = 0x07064B50;
constexpr u32 kZip64EocdSignature = 0x06064B50;
constexpr u32 kCentralSignature = 0x02014B50;
constexpr u32 kLocalSignature = 0x04034B50;
constexpr std::size_t kEocdSize = 22;
constexpr std::size_t kZip64LocatorSize = 20;
constexpr std::size_t kZip64EocdSize = 56;
constexpr std::size_t kCentralSize = 46;
constexpr std::size_t kLocalSize = 30;
constexpr std::size_t kMaxComment = 0xFFFF;
constexpr u16 kZip64ExtraId = 0x0001;
constexpr u32 kSaturated32 = 0xFFFF'FFFFu;
constexpr u16 kSaturated16 = 0xFFFF;
constexpr u16 kStored = 0;
// Far above any real build's directory; a larger claim is damage, not something to allocate.
constexpr u64 kMaxCentralDirectory = u64{256} << 20;

[[nodiscard]] Diagnostic corrupt(std::string entry = {}) {
    return to_diagnostic(
        BuildsError{.code = BuildsErrorCode::CorruptArchive, .path = NativePath{}, .archive_entry = std::move(entry)});
}

[[nodiscard]] u16 le16(std::span<const u8> bytes, std::size_t at) noexcept {
    return static_cast<u16>(bytes[at] | (bytes[at + 1] << 8));
}

[[nodiscard]] u32 le32(std::span<const u8> bytes, std::size_t at) noexcept {
    return static_cast<u32>(le16(bytes, at)) | (static_cast<u32>(le16(bytes, at + 2)) << 16);
}

[[nodiscard]] u64 le64(std::span<const u8> bytes, std::size_t at) noexcept {
    return static_cast<u64>(le32(bytes, at)) | (static_cast<u64>(le32(bytes, at + 4)) << 32);
}

// A range past the end is corrupt, not a read failure.
Result<std::vector<u8>> read(IByteSource& source, u64 offset, u64 length, std::string_view entry = {}) {
    if (offset > source.size() || length > source.size() - offset) return std::unexpected(corrupt(std::string(entry)));
    std::vector<u8> bytes(static_cast<std::size_t>(length));
    if (auto ok = source.read_at(offset, bytes); !ok) return std::unexpected(std::move(ok.error()));
    return bytes;
}

template <std::size_t N>
[[nodiscard]] bool starts_with(std::span<const u8> bytes, const std::array<u8, N>& signature) noexcept {
    return bytes.size() >= N && std::equal(signature.begin(), signature.end(), bytes.begin());
}

struct Directory {
    u64 entries = 0;
    u64 size = 0;
    u64 offset = 0;
};

Result<Directory> find_directory(IByteSource& source) {
    const u64 size = source.size();
    if (size < kEocdSize) return std::unexpected(corrupt());
    const u64 tail_length = std::min<u64>(size, kEocdSize + kMaxComment);
    auto tail = read(source, size - tail_length, tail_length);
    if (!tail) return std::unexpected(std::move(tail.error()));

    std::optional<std::size_t> eocd;
    for (std::size_t at = tail->size() - kEocdSize + 1; at-- > 0;) {
        if (le32(*tail, at) == kEocdSignature) {
            eocd = at;
            break;
        }
    }
    if (!eocd) return std::unexpected(corrupt());
    const std::span<const u8> record(tail->data() + *eocd, kEocdSize);
    Directory directory{.entries = le16(record, 10), .size = le32(record, 12), .offset = le32(record, 16)};

    const bool zip64 = directory.entries == kSaturated16 || directory.size == kSaturated32 ||
                       directory.offset == kSaturated32;
    const u64 eocd_offset = size - tail_length + *eocd;
    if (!zip64 || eocd_offset < kZip64LocatorSize) return directory;

    auto locator = read(source, eocd_offset - kZip64LocatorSize, kZip64LocatorSize);
    if (!locator) return std::unexpected(std::move(locator.error()));
    if (le32(*locator, 0) != kZip64LocatorSignature) return directory;
    auto record64 = read(source, le64(*locator, 8), kZip64EocdSize);
    if (!record64) return std::unexpected(std::move(record64.error()));
    if (le32(*record64, 0) != kZip64EocdSignature) return std::unexpected(corrupt());
    return Directory{.entries = le64(*record64, 32), .size = le64(*record64, 40), .offset = le64(*record64, 48)};
}

// Replaces saturated 32-bit fields with the ZIP64 extra's values, which appear in this order.
void apply_zip64_extra(std::span<const u8> extra, ZipEntry& entry, bool wide_uncompressed, bool wide_compressed,
                       bool wide_offset) {
    std::size_t at = 0;
    while (at + 4 <= extra.size()) {
        const u16 id = le16(extra, at);
        const u16 length = le16(extra, at + 2);
        const std::size_t data = at + 4;
        if (data + length > extra.size()) return;
        if (id == kZip64ExtraId) {
            std::size_t field = data;
            const auto take = [&](u64& out) {
                if (field + 8 > data + length) return;
                out = le64(extra, field);
                field += 8;
            };
            if (wide_uncompressed) take(entry.uncompressed_size);
            if (wide_compressed) take(entry.compressed_size);
            if (wide_offset) take(entry.local_header_offset);
            return;
        }
        at = data + length;
    }
}

}  // namespace

Result<std::vector<ZipEntry>> read_zip_directory(IByteSource& source) {
    auto directory = find_directory(source);
    if (!directory) return std::unexpected(std::move(directory.error()));
    // Each record is at least kCentralSize, which bounds the count by the directory's size.
    if (directory->size > kMaxCentralDirectory || directory->entries > directory->size / kCentralSize + 1)
        return std::unexpected(corrupt());
    auto bytes = read(source, directory->offset, directory->size);
    if (!bytes) return std::unexpected(std::move(bytes.error()));

    std::vector<ZipEntry> entries;
    entries.reserve(static_cast<std::size_t>(directory->entries));
    std::size_t at = 0;
    for (u64 i = 0; i < directory->entries; ++i) {
        if (at + kCentralSize > bytes->size() || le32(*bytes, at) != kCentralSignature)
            return std::unexpected(corrupt());
        const std::span<const u8> header(bytes->data() + at, kCentralSize);
        const u16 name_length = le16(header, 28);
        const u16 extra_length = le16(header, 30);
        const u16 comment_length = le16(header, 32);
        const std::size_t name_at = at + kCentralSize;
        const std::size_t extra_at = name_at + name_length;
        const std::size_t next = extra_at + extra_length + comment_length;
        if (next > bytes->size()) return std::unexpected(corrupt());

        ZipEntry entry{.name = std::string(reinterpret_cast<const char*>(bytes->data() + name_at), name_length),
                       .flags = le16(header, 8),
                       .method = le16(header, 10),
                       .compressed_size = le32(header, 20),
                       .uncompressed_size = le32(header, 24),
                       .local_header_offset = le32(header, 42)};
        apply_zip64_extra(std::span<const u8>(bytes->data() + extra_at, extra_length), entry,
                          entry.uncompressed_size == kSaturated32, entry.compressed_size == kSaturated32,
                          entry.local_header_offset == kSaturated32);
        entries.push_back(std::move(entry));
        at = next;
    }
    return entries;
}

Result<u64> zip_data_offset(IByteSource& source, const ZipEntry& entry) {
    auto header = read(source, entry.local_header_offset, kLocalSize, entry.name);
    if (!header) return std::unexpected(std::move(header.error()));
    if (le32(*header, 0) != kLocalSignature) return std::unexpected(corrupt(entry.name));
    const u64 data = entry.local_header_offset + kLocalSize + le16(*header, 26) + le16(*header, 28);
    if (data > source.size() || entry.compressed_size > source.size() - data)
        return std::unexpected(corrupt(entry.name));
    return data;
}

Result<ArchiveProbe> probe_archive(IByteSource& source) {
    std::array<u8, 8> head{};
    const auto head_length = static_cast<std::size_t>(std::min<u64>(source.size(), head.size()));
    if (auto ok = source.read_at(0, std::span<u8>(head.data(), head_length)); !ok)
        return std::unexpected(std::move(ok.error()));
    const std::span<const u8> magic(head.data(), head_length);

    if (starts_with(magic, kRar4) || starts_with(magic, kRar5))
        return ArchiveProbe{.format = catalog::ArchiveFormat::Rar};
    if (starts_with(magic, kSevenZip)) return ArchiveProbe{.format = catalog::ArchiveFormat::SevenZip};
    if (!starts_with(magic, kZipLocal) && !starts_with(magic, kZipEmpty))
        return std::unexpected(
            to_diagnostic(BuildsError{.code = BuildsErrorCode::UnsupportedArchive, .path = NativePath{}}));

    auto entries = read_zip_directory(source);
    if (!entries) return std::unexpected(std::move(entries.error()));
    const ArchiveProbe plain{.format = catalog::ArchiveFormat::Zip};
    if (entries->size() != 1 || entries->front().method != kStored) return plain;

    const ZipEntry& entry = entries->front();
    auto data = zip_data_offset(source, entry);
    if (!data) return std::unexpected(std::move(data.error()));
    if (entry.compressed_size < kSevenZip.size()) return plain;
    auto inner = read(source, *data, kSevenZip.size(), entry.name);
    if (!inner) return std::unexpected(std::move(inner.error()));
    if (!starts_with(*inner, kSevenZip)) return plain;
    return ArchiveProbe{.format = catalog::ArchiveFormat::SevenZip,
                        .container = catalog::ArchiveContainer::ZipStored,
                        .window = ByteWindow{.offset = *data, .length = entry.compressed_size}};
}

}  // namespace rb::builds
