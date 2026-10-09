#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "builds_test_support.hpp"
#include "reboot/builds/cl_table.hpp"
#include "reboot/builds/file_byte_source.hpp"
#include "reboot/builds/memory_byte_source.hpp"
#include "reboot/builds/pe_version_reader.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/testing/golden.hpp"

using namespace reboot;
using namespace reboot::builds;
using namespace reboot::builds::test;

namespace {

std::vector<u8> golden_blob(std::string_view name) {
    auto bytes = testing::read_golden(NativePath(REBOOT_BUILDS_TEST_DATA) / NativePath(name));
    REQUIRE(bytes);
    return *bytes;
}

Diagnostic read_error(const std::vector<u8>& bytes) {
    MemoryByteSource source(bytes);
    auto resource = PeVersionReader{}.read_version_resource(source);
    REQUIRE_FALSE(resource);
    return resource.error();
}

// Release text at `offset` in a buffer of `size` bytes, as a raw scan meets it in an exe.
std::vector<u8> raw_with(std::size_t size, std::size_t offset, std::string_view text) {
    std::vector<u8> bytes(size, 0x11);
    const std::vector<u8> encoded = utf16le(text);
    std::ranges::copy(encoded, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    bytes[offset + encoded.size()] = 0;
    bytes[offset + encoded.size() + 1] = 0;
    return bytes;
}

}  // namespace

TEST_CASE("the PE reader returns the RT_VERSION blob through all three directory levels") {
    const std::vector<u8> blob = golden_blob("version_release_3_5.bin");
    const std::vector<u8> pe = make_pe(blob);
    MemoryByteSource source(pe);
    auto resource = PeVersionReader{}.read_version_resource(source);
    REQUIRE(resource);
    CHECK(*resource == blob);

    auto marker = PeVersionReader{}.read_marker(source);
    REQUIRE(marker);
    REQUIRE(*marker);
    CHECK((*marker)->tail == "3.5");
    CHECK((*marker)->engine_cl == Changelist{4008490});
}

TEST_CASE("read_marker is empty when the resource names no release") {
    const std::vector<u8> pe = make_pe(version_blob("CompanyName Epic Games"));
    MemoryByteSource source(pe);
    auto marker = PeVersionReader{}.read_marker(source);
    REQUIRE(marker);
    CHECK_FALSE(*marker);
}

TEST_CASE("the PE reader refuses what is not a Windows executable") {
    CHECK(read_error({}).id == "builds.pe_not_pe");
    CHECK(read_error(std::vector<u8>(512, 0x41)).id == "builds.pe_not_pe");

    std::vector<u8> no_signature = make_pe(version_blob("x"));
    no_signature[0x40] = 'X';
    CHECK(read_error(no_signature).id == "builds.pe_not_pe");

    std::vector<u8> far_header = make_pe(version_blob("x"));
    put32(far_header, 0x3C, 0x7FFF'FFF0u);
    CHECK(read_error(far_header).id == "builds.pe_not_pe");
}

TEST_CASE("every offset, size and count is checked before it is followed") {
    const std::vector<u8> good = make_pe(version_blob("++Fortnite+Release-8.51"));

    SECTION("a section table past the end") {
        std::vector<u8> pe = good;
        put16(pe, 0x46, 0xFFFF);
        CHECK(read_error(pe).id == "builds.pe_malformed");
    }
    SECTION("an optional header of an unknown kind") {
        std::vector<u8> pe = good;
        put16(pe, 0x40 + 24, 0x1234);
        CHECK(read_error(pe).id == "builds.pe_malformed");
    }
    SECTION("a subdirectory offset outside the resource directory") {
        std::vector<u8> pe = good;
        put32(pe, 0x200 + 20, 0x8000'0000u | 0x00FF'FFFFu);
        CHECK(read_error(pe).id == "builds.pe_malformed");
    }
    SECTION("an entry count larger than the directory") {
        std::vector<u8> pe = good;
        put16(pe, 0x200 + 14, 0xFFFF);
        CHECK(read_error(pe).id == "builds.pe_malformed");
    }
    SECTION("data that lies outside its section's raw bytes") {
        std::vector<u8> pe = good;
        put32(pe, 0x200 + 72, 0x0090'0000u);
        CHECK(read_error(pe).id == "builds.pe_malformed");
    }
    SECTION("a file cut short") {
        std::vector<u8> pe = good;
        pe.resize(0x200 + 40);
        CHECK(read_error(pe).id == "builds.pe_malformed");
    }
}

TEST_CASE("a missing RT_VERSION is reported as such") {
    std::vector<u8> pe = make_pe(version_blob("x"));
    put32(pe, 0x200 + 16, 3);
    CHECK(read_error(pe).id == "builds.pe_no_version_resource");

    std::vector<u8> no_directory = make_pe(version_blob("x"));
    put32(no_directory, 0x40 + 24 + 112 + 16, 0);
    CHECK(read_error(no_directory).id == "builds.pe_no_version_resource");
}

TEST_CASE("a version resource above the cap is refused") {
    std::vector<u8> pe = make_pe(version_blob("x"));
    put32(pe, 0x200 + 76, static_cast<u32>(kVersionResourceCap + 1));
    const Diagnostic diag = read_error(pe);
    CHECK(diag.id == "builds.pe_resource_too_large");
    CHECK(arg_text(diag, "limit") == std::to_string(kVersionResourceCap));
}

TEST_CASE("the raw scan finds a marker anywhere, including across chunk edges") {
    const PeVersionReader reader;
    constexpr std::size_t kMiB = std::size_t{1} << 20;
    const std::string text = "4.20.0-4008490+++Fortnite+Release-3.5";
    for (const std::size_t offset : {std::size_t{0}, std::size_t{1}, kMiB - 40, kMiB - 1, kMiB + 7, 2 * kMiB - 3}) {
        INFO(offset);
        const std::vector<u8> bytes = raw_with(3 * kMiB, offset, text);
        MemoryByteSource source(bytes);
        auto marker = reader.scan_for_marker(source, CancelToken{});
        REQUIRE(marker);
        REQUIRE(*marker);
        CHECK((*marker)->tail == "3.5");
        CHECK((*marker)->engine_cl == Changelist{4008490});
    }

    const std::vector<u8> at_end = raw_with(kMiB + 200, kMiB + 200 - 80, "++Fortnite+Release-12.41");
    MemoryByteSource end_source(at_end);
    auto marker = reader.scan_for_marker(end_source, CancelToken{});
    REQUIRE(marker);
    REQUIRE(*marker);
    CHECK((*marker)->tail == "12.41");
}

TEST_CASE("the raw scan returns nothing for a file without a marker, and stops when cancelled") {
    const PeVersionReader reader;
    const std::vector<u8> bytes(3 * (std::size_t{1} << 20), 0x22);
    MemoryByteSource source(bytes);
    auto none = reader.scan_for_marker(source, CancelToken{});
    REQUIRE(none);
    CHECK_FALSE(*none);

    CancelSource cancel;
    cancel.cancel(CancelReason::User);
    auto cancelled = reader.scan_for_marker(source, cancel.token());
    REQUIRE_FALSE(cancelled);
    CHECK(cancelled.error().id == "builds.cancelled");
    CHECK(cancelled.error().kind == ErrorKind::Cancelled);
}

TEST_CASE("byte sources read exact ranges and refuse the rest") {
    const std::vector<u8> bytes{1, 2, 3, 4};
    MemoryByteSource memory(bytes);
    std::vector<u8> out(2);
    REQUIRE(memory.read_at(2, out));
    CHECK(out == std::vector<u8>{3, 4});
    CHECK_FALSE(memory.read_at(3, out));
    CHECK_FALSE(memory.read_at(~u64{0}, out));

    const testing::ScratchDir dir = make_scratch("reboot-builds-bytes");
    const NativePath file = dir.path() / "bytes.bin";
    write_file(file, bytes);
    Result<FileByteSource> source = FileByteSource::open(file);
    REQUIRE(source);
    CHECK(source->size() == 4);
    REQUIRE(source->read_at(1, out));
    CHECK(out == std::vector<u8>{2, 3});
    const auto past = source->read_at(3, out);
    REQUIRE_FALSE(past);
    CHECK(past.error().id == "builds.io");

    const auto missing = FileByteSource::open(dir.path() / "missing.bin");
    REQUIRE_FALSE(missing);
    CHECK(missing.error().id == "builds.io");
}

TEST_CASE("the compiled CL table is sorted, canonical and carries the corrected labels") {
    const std::span<const ClTableEntry> entries = compiled_cl_entries();
    REQUIRE(entries.size() > 100);
    CHECK(std::ranges::is_sorted(entries, std::less<>{}, &ClTableEntry::changelist));
    CHECK(std::ranges::adjacent_find(entries, {}, &ClTableEntry::changelist) == entries.end());
    for (const ClTableEntry& entry : entries) {
        INFO(entry.changelist);
        const auto parsed = GameVersion::parse(entry.version);
        REQUIRE(parsed);
        CHECK(parsed->canonical() == entry.version);
    }
    const ClTable table;
    CHECK(table.lookup(Changelist{4461277}) == version("6.2"));
    CHECK(table.lookup(Changelist{3700114}) == version("1.7.2"));
    CHECK(table.lookup(Changelist{3870737}) == version("2.4.2"));
    CHECK_FALSE(table.lookup(Changelist{3700115}));
}
