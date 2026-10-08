#include "reboot/compat/pe_imports.hpp"

#include <cstddef>
#include <optional>
#include <utility>
#include <string_view>

#include "messages.hpp"
#include "reboot/foundation/text.hpp"

namespace reboot::compat {

namespace {

constexpr u16 kPe32PlusMagic = 0x20B;
constexpr std::size_t kImportDirectory = 1;
constexpr std::size_t kDelayImportDirectory = 13;
constexpr std::size_t kImportDescriptorSize = 20;
constexpr std::size_t kDelayDescriptorSize = 32;
constexpr std::size_t kSectionHeaderSize = 40;

struct Section {
    u32 virtual_address = 0;
    u32 virtual_size = 0;
    u32 raw_size = 0;
    u32 raw_offset = 0;
};

class PeReader {
public:
    PeReader(const NativePath& path, std::span<const u8> image) : path_(path), image_(image) {}

    [[nodiscard]] Result<std::vector<std::string>> imports() {
        const auto dirs = read_headers();
        if (!dirs) return std::unexpected(dirs.error());
        std::vector<std::string> names;
        if (auto ok = read_descriptors(dirs->imports, kImportDescriptorSize, 12, names); !ok)
            return std::unexpected(ok.error());
        if (auto ok = read_descriptors(dirs->delay_imports, kDelayDescriptorSize, 4, names); !ok)
            return std::unexpected(ok.error());
        return names;
    }

private:
    struct Directory {
        u32 rva = 0;
        std::size_t entry_offset = 0;
    };

    struct Directories {
        Directory imports;
        Directory delay_imports;
    };

    [[nodiscard]] std::optional<u32> read32(std::size_t offset) const noexcept {
        if (offset > image_.size() || image_.size() - offset < 4) return std::nullopt;
        return static_cast<u32>(image_[offset]) | static_cast<u32>(image_[offset + 1]) << 8 |
               static_cast<u32>(image_[offset + 2]) << 16 | static_cast<u32>(image_[offset + 3]) << 24;
    }

    [[nodiscard]] std::optional<u16> read16(std::size_t offset) const noexcept {
        if (offset > image_.size() || image_.size() - offset < 2) return std::nullopt;
        return static_cast<u16>(image_[offset] | image_[offset + 1] << 8);
    }

    [[nodiscard]] Diagnostic malformed(std::size_t offset) const {
        return make_diag(ErrorDomain::Compat, msg::kPeMalformed)
            .arg("path", path_)
            .arg("offset", static_cast<u64>(offset))
            .kind(ErrorKind::InvalidInput)
            .build();
    }

    [[nodiscard]] Result<Directories> read_headers() {
        const auto mz = read16(0);
        if (!mz || *mz != 0x5A4D) return std::unexpected(malformed(0));
        const auto lfanew = read32(0x3C);
        if (!lfanew) return std::unexpected(malformed(0x3C));
        const std::size_t pe = *lfanew;
        const auto signature = read32(pe);
        if (!signature || *signature != 0x00004550) return std::unexpected(malformed(pe));
        const auto section_count = read16(pe + 6);
        const auto optional_size = read16(pe + 20);
        if (!section_count || !optional_size) return std::unexpected(malformed(pe + 6));
        const std::size_t optional = pe + 24;
        const auto magic = read16(optional);
        if (!magic || *magic != kPe32PlusMagic) return std::unexpected(malformed(optional));
        const auto directory_count = read32(optional + 108);
        if (!directory_count) return std::unexpected(malformed(optional + 108));

        const std::size_t sections = optional + *optional_size;
        for (std::size_t i = 0; i < *section_count; ++i) {
            const std::size_t at = sections + i * kSectionHeaderSize;
            const auto virtual_size = read32(at + 8);
            const auto virtual_address = read32(at + 12);
            const auto raw_size = read32(at + 16);
            const auto raw_offset = read32(at + 20);
            if (!virtual_size || !virtual_address || !raw_size || !raw_offset) return std::unexpected(malformed(at));
            sections_.push_back({*virtual_address, *virtual_size, *raw_size, *raw_offset});
        }

        Directories dirs;
        const auto directory = [&](std::size_t index) -> Result<Directory> {
            if (index >= *directory_count) return Directory{};
            const std::size_t at = optional + 112 + index * 8;
            const auto rva = read32(at);
            if (!rva || at + 8 > sections) return std::unexpected(malformed(at));
            return Directory{*rva, at};
        };
        const auto imports = directory(kImportDirectory);
        if (!imports) return std::unexpected(imports.error());
        const auto delay_imports = directory(kDelayImportDirectory);
        if (!delay_imports) return std::unexpected(delay_imports.error());
        dirs.imports = *imports;
        dirs.delay_imports = *delay_imports;
        return dirs;
    }

    [[nodiscard]] std::optional<std::size_t> to_offset(u32 rva) const noexcept {
        for (const Section& section : sections_) {
            if (rva < section.virtual_address) continue;
            const u32 delta = rva - section.virtual_address;
            if (delta >= section.raw_size) continue;
            const std::size_t offset = std::size_t{section.raw_offset} + delta;
            if (offset >= image_.size()) return std::nullopt;
            return offset;
        }
        return std::nullopt;
    }

    [[nodiscard]] Result<std::string> read_name(u32 rva, std::size_t referenced_at) const {
        const auto offset = to_offset(rva);
        if (!offset) return std::unexpected(malformed(referenced_at));
        const auto bytes = image_.subspan(*offset);
        for (std::size_t i = 0; i < bytes.size(); ++i)
            if (bytes[i] == 0) return std::string(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(i));
        return std::unexpected(malformed(*offset));
    }

    // Walks a descriptor table until the entry whose name RVA is zero.
    [[nodiscard]] Result<void> read_descriptors(Directory directory, std::size_t entry_size, std::size_t name_field,
                                                std::vector<std::string>& names) const {
        if (directory.rva == 0) return {};
        const auto table = to_offset(directory.rva);
        if (!table) return std::unexpected(malformed(directory.entry_offset));
        for (std::size_t at = *table;; at += entry_size) {
            if (at > image_.size() || image_.size() - at < entry_size) return std::unexpected(malformed(at));
            const auto name_rva = read32(at + name_field);
            if (*name_rva == 0) return {};
            auto name = read_name(*name_rva, at + name_field);
            if (!name) return std::unexpected(name.error());
            names.push_back(std::move(*name));
        }
    }

    const NativePath& path_;
    std::span<const u8> image_;
    std::vector<Section> sections_;
};

[[nodiscard]] bool starts_with_ascii_nocase(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && iequals_ascii(text.substr(0, prefix.size()), prefix);
}

}  // namespace

Result<std::vector<std::string>> read_pe_imports(const NativePath& path, std::span<const u8> image) {
    return PeReader(path, image).imports();
}

bool needs_vc_runtime(std::span<const std::string> imports) noexcept {
    for (const std::string& name : imports)
        if (starts_with_ascii_nocase(name, "msvcp140") || starts_with_ascii_nocase(name, "vcruntime140")) return true;
    return false;
}

}  // namespace reboot::compat
