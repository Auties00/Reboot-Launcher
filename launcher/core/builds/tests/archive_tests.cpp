#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "builds_test_support.hpp"
#include "reboot/builds/archive_probe.hpp"
#include "reboot/builds/byte_source.hpp"
#include "reboot/builds/libarchive_extractor.hpp"
#include "reboot/builds/memory_byte_source.hpp"
#include "reboot/foundation/cancel.hpp"

// Last: it brings in libarchive, and with it windows.h.
#include "archive_writer.hpp"

using namespace rb;
using namespace rb::builds;
using namespace rb::builds::test;

namespace {

void add(std::vector<u8>& out, u64 value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) out.push_back(static_cast<u8>(value >> (8 * i)));
}

// A ZIP with one stored entry and ZIP64 records, built by hand so every size field is known.
std::vector<u8> zip64_wrapping(const std::vector<u8>& payload) {
    const std::string name = "inner.7z";
    std::vector<u8> out;
    add(out, 0x04034B50, 4);
    add(out, 45, 2);
    add(out, 0, 2);
    add(out, 0, 2);
    add(out, 0, 4);
    add(out, 0, 4);
    add(out, 0xFFFF'FFFF, 4);
    add(out, 0xFFFF'FFFF, 4);
    add(out, name.size(), 2);
    add(out, 20, 2);
    out.insert(out.end(), name.begin(), name.end());
    add(out, 0x0001, 2);
    add(out, 16, 2);
    add(out, payload.size(), 8);
    add(out, payload.size(), 8);
    const u64 data = out.size();
    out.insert(out.end(), payload.begin(), payload.end());

    const u64 central = out.size();
    add(out, 0x02014B50, 4);
    add(out, 45, 2);
    add(out, 45, 2);
    add(out, 0, 2);
    add(out, 0, 2);
    add(out, 0, 4);
    add(out, 0, 4);
    add(out, 0xFFFF'FFFF, 4);
    add(out, 0xFFFF'FFFF, 4);
    add(out, name.size(), 2);
    add(out, 28, 2);
    add(out, 0, 2);
    add(out, 0, 2);
    add(out, 0, 2);
    add(out, 0, 4);
    add(out, 0xFFFF'FFFF, 4);
    out.insert(out.end(), name.begin(), name.end());
    add(out, 0x0001, 2);
    add(out, 24, 2);
    add(out, payload.size(), 8);
    add(out, payload.size(), 8);
    add(out, 0, 8);
    const u64 central_size = out.size() - central;

    const u64 record = out.size();
    add(out, 0x06064B50, 4);
    add(out, 44, 8);
    add(out, 45, 2);
    add(out, 45, 2);
    add(out, 0, 4);
    add(out, 0, 4);
    add(out, 1, 8);
    add(out, 1, 8);
    add(out, central_size, 8);
    add(out, central, 8);
    add(out, 0x07064B50, 4);
    add(out, 0, 4);
    add(out, record, 8);
    add(out, 1, 4);
    add(out, 0x06054B50, 4);
    add(out, 0, 2);
    add(out, 0, 2);
    add(out, 0xFFFF, 2);
    add(out, 0xFFFF, 2);
    add(out, 0xFFFF'FFFF, 4);
    add(out, 0xFFFF'FFFF, 4);
    add(out, 0, 2);
    (void)data;
    return out;
}

// The ZIP64 end records of a file of `size` bytes whose directory is `directory_size` bytes at
// `directory_offset`, holding `entries`; the records end the file.
std::vector<u8> zip64_tail(u64 size, u64 entries, u64 directory_size, u64 directory_offset) {
    std::vector<u8> out;
    add(out, 0x06064B50, 4);
    add(out, 44, 8);
    add(out, 45, 2);
    add(out, 45, 2);
    add(out, 0, 4);
    add(out, 0, 4);
    add(out, entries, 8);
    add(out, entries, 8);
    add(out, directory_size, 8);
    add(out, directory_offset, 8);
    add(out, 0x07064B50, 4);
    add(out, 0, 4);
    add(out, size - 98, 8);
    add(out, 1, 4);
    add(out, 0x06054B50, 4);
    add(out, 0, 2);
    add(out, 0, 2);
    add(out, 0xFFFF, 2);
    add(out, 0xFFFF, 2);
    add(out, 0xFFFF'FFFF, 4);
    add(out, 0xFFFF'FFFF, 4);
    add(out, 0, 2);
    return out;
}

// Claims `size` bytes but holds only a head and a tail, zeros between; records every read.
class SparseByteSource final : public IByteSource {
public:
    SparseByteSource(u64 size, std::vector<u8> head, std::vector<u8> tail)
        : size_(size), head_(std::move(head)), tail_(std::move(tail)) {}

    [[nodiscard]] u64 size() const noexcept override { return size_; }

    Result<void> read_at(u64 offset, std::span<u8> out) override {
        reads.emplace_back(offset, out.size());
        const u64 tail_start = size_ - tail_.size();
        for (std::size_t i = 0; i < out.size(); ++i) {
            const u64 at = offset + i;
            if (at < head_.size()) out[i] = head_[static_cast<std::size_t>(at)];
            else if (at >= tail_start) out[i] = tail_[static_cast<std::size_t>(at - tail_start)];
            else out[i] = 0;
        }
        return {};
    }

    std::vector<std::pair<u64, std::size_t>> reads;

private:
    u64 size_;
    std::vector<u8> head_;
    std::vector<u8> tail_;
};

// libarchive's own test_read_format_rar5_win32.rar: non-solid, one folder, test.bin stored and
// test1.bin to test6.bin compressed.
const NativePath kRar5 = NativePath(REBOOT_BUILDS_TEST_DATA) / "rar5_win32.rar";

// The bytes libarchive's verify_data() expects of the RAR5 fixture's files.
std::vector<u8> rar5_contents(i64 magic, std::size_t size) {
    std::vector<u8> out;
    for (i64 k = 1; k <= static_cast<i64>(size / 4); ++k)
        add(out, static_cast<u64>(std::max<i64>(k * k - 3 * k + 1 + magic, 0)), 4);
    return out;
}

struct Fixture {
    testing::ScratchDir dir = make_scratch("reboot-builds-archive");
    NativePath archive = dir.path() / "archive";
    NativePath destination = dir.path() / "content";
    LibArchiveExtractor extractor;
    std::vector<ExtractProgress> progress;

    Fixture() { std::filesystem::create_directories(destination); }

    Result<ExtractSummary> extract(const std::vector<u8>& bytes, CancelToken token = {}) {
        write_file(archive, bytes);
        return extractor.extract(ExtractRequest{.archive = archive, .destination = destination}, token,
                                 [this](const ExtractProgress& p) { progress.push_back(p); });
    }

    std::string text(const NativePath& relative) const {
        const std::vector<u8> bytes = read_file(destination / relative);
        return {bytes.begin(), bytes.end()};
    }
};

const std::vector<Entry> kBuild{
    {"Build/", "", true},
    {"Build/FortniteGame/Binaries/Win64/FortniteClient-Win64-Shipping.exe", std::string(70000, 'x')},
    {"Build/Engine/Binaries/Win64/CrashReportClient.exe", "crash"},
    {"Build/empty.txt", ""},
};

}  // namespace

TEST_CASE("the probe reads signatures, never extensions") {
    const std::vector<u8> zip = make_archive("zip", {{"a.txt", "a"}});
    MemoryByteSource zip_source(zip);
    auto zip_probe = probe_archive(zip_source);
    REQUIRE(zip_probe);
    CHECK(zip_probe->format == catalog::ArchiveFormat::Zip);
    CHECK(zip_probe->container == catalog::ArchiveContainer::None);
    CHECK_FALSE(zip_probe->window);

    const std::vector<u8> seven = make_archive("7zip", {{"a.txt", "a"}});
    MemoryByteSource seven_source(seven);
    auto seven_probe = probe_archive(seven_source);
    REQUIRE(seven_probe);
    CHECK(seven_probe->format == catalog::ArchiveFormat::SevenZip);

    const std::vector<u8> rar5 = read_file(kRar5);
    MemoryByteSource rar_source(rar5);
    auto rar_probe = probe_archive(rar_source);
    REQUIRE(rar_probe);
    CHECK(rar_probe->format == catalog::ArchiveFormat::Rar);

    const std::vector<u8> unknown(64, 0x42);
    MemoryByteSource unknown_source(unknown);
    auto refused = probe_archive(unknown_source);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "builds.unsupported_archive");
}

TEST_CASE("a ZIP holding one stored 7z is probed as a window onto it") {
    const std::vector<u8> seven = make_archive("7zip", kBuild);
    const std::vector<u8> wrapped = make_archive("zip", {{"windows-12.41.7z", std::string(seven.begin(), seven.end())}},
                                                 true);
    MemoryByteSource source(wrapped);
    auto probe = probe_archive(source);
    REQUIRE(probe);
    CHECK(probe->format == catalog::ArchiveFormat::SevenZip);
    CHECK(probe->container == catalog::ArchiveContainer::ZipStored);
    REQUIRE(probe->window);
    CHECK(probe->window->length == seven.size());
    CHECK(std::equal(seven.begin(), seven.end(), wrapped.begin() + static_cast<std::ptrdiff_t>(probe->window->offset)));

    // Deflated, the inner bytes cannot be read in place, so it is an ordinary ZIP.
    const std::vector<u8> deflated = make_archive("zip", {{"inner.7z", std::string(seven.begin(), seven.end())}});
    MemoryByteSource deflated_source(deflated);
    auto plain = probe_archive(deflated_source);
    REQUIRE(plain);
    CHECK(plain->format == catalog::ArchiveFormat::Zip);
}

TEST_CASE("ZIP64 records give the window of a large stored entry") {
    const std::vector<u8> seven = make_archive("7zip", {{"a.txt", "a"}});
    const std::vector<u8> zip = zip64_wrapping(seven);
    MemoryByteSource source(zip);
    auto probe = probe_archive(source);
    REQUIRE(probe);
    CHECK(probe->container == catalog::ArchiveContainer::ZipStored);
    REQUIRE(probe->window);
    CHECK(probe->window->offset == 30 + 8 + 20);
    CHECK(probe->window->length == seven.size());
}

TEST_CASE("a ZIP whose directory is damaged is corrupt") {
    std::vector<u8> zip = make_archive("zip", {{"a.txt", "a"}});
    zip.resize(zip.size() - 10);
    MemoryByteSource source(zip);
    auto probe = probe_archive(source);
    REQUIRE_FALSE(probe);
    CHECK(probe.error().id == "builds.corrupt_archive");
}

TEST_CASE("a ZIP directory above the size cap is corrupt before anything is read from it") {
    constexpr u64 kSize = u64{1} << 30;
    constexpr u64 kCap = u64{256} << 20;
    const std::vector<u8> head{'P', 'K', 3, 4};

    const u64 oversized_at = kSize - 98 - (kCap + 1);
    SparseByteSource oversized(kSize, head, zip64_tail(kSize, 1, kCap + 1, oversized_at));
    auto refused = probe_archive(oversized);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "builds.corrupt_archive");
    for (const auto& [offset, length] : oversized.reads) {
        CHECK(offset != oversized_at);
        CHECK(length <= 22 + 0xFFFF);
    }

    // More entries than the directory's size can hold would size a reserve() from damage.
    const u64 crowded_at = kSize - 98 - 92;
    SparseByteSource crowded(kSize, head, zip64_tail(kSize, u64{1} << 40, 92, crowded_at));
    auto crowded_probe = probe_archive(crowded);
    REQUIRE_FALSE(crowded_probe);
    CHECK(crowded_probe.error().id == "builds.corrupt_archive");
    for (const auto& [offset, length] : crowded.reads) CHECK(offset != crowded_at);
}

TEST_CASE("a RAR5 archive extracts its folder, stored and compressed files") {
    Fixture f;
    auto summary = f.extract(read_file(kRar5));
    REQUIRE(summary);
    CHECK(summary->probe.format == catalog::ArchiveFormat::Rar);
    CHECK(summary->entries == 8);
    CHECK(summary->bytes == 1200 + 6 * 4096);
    CHECK(std::filesystem::is_directory(f.destination / "testdir"));
    CHECK(read_file(f.destination / "test.bin") == rar5_contents(0, 1200));
    for (int n = 1; n <= 6; ++n) {
        INFO(n);
        CHECK(read_file(f.destination / ("test" + std::to_string(n) + ".bin")) == rar5_contents(n, 4096));
    }
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(f.destination))
        if (entry.is_regular_file()) ++files;
    CHECK(files == 7);
    REQUIRE_FALSE(f.progress.empty());
    CHECK(f.progress.back().entries_done == 8);
    CHECK(f.progress.back().bytes_done == 1200 + 6 * 4096);
    // Only a ZIP's directory states the total up front.
    CHECK_FALSE(f.progress.back().bytes_total);
}

TEST_CASE("a cancelled RAR5 extraction stops before its next entry") {
    Fixture f;
    write_file(f.archive, read_file(kRar5));
    CancelSource cancel;
    auto cancelled = f.extractor.extract(
        ExtractRequest{.archive = f.archive, .destination = f.destination}, cancel.token(),
        [&](const ExtractProgress& progress) {
            f.progress.push_back(progress);
            if (progress.entries_done == 2) cancel.cancel(CancelReason::User);
        });
    REQUIRE_FALSE(cancelled);
    CHECK(cancelled.error().id == "builds.cancelled");
    CHECK(f.progress.back().entries_done == 2);
    CHECK(std::filesystem::is_regular_file(f.destination / "test.bin"));
    CHECK_FALSE(std::filesystem::exists(f.destination / "test1.bin"));
}

TEST_CASE("ZIP, 7z and a ZIP-wrapped 7z extract the same tree") {
    const std::vector<u8> seven = make_archive("7zip", kBuild);
    const std::vector<std::pair<std::string, std::vector<u8>>> shapes{
        {"zip", make_archive("zip", kBuild)},
        {"7z", seven},
        {"wrapped", make_archive("zip", {{"inner.7z", std::string(seven.begin(), seven.end())}}, true)},
    };
    for (const auto& [shape, bytes] : shapes) {
        INFO(shape);
        Fixture f;
        auto summary = f.extract(bytes);
        REQUIRE(summary);
        CHECK(summary->entries == 4);
        CHECK(summary->bytes == 70000 + 5);
        CHECK(f.text(NativePath("Build") / "Engine" / "Binaries" / "Win64" / "CrashReportClient.exe") == "crash");
        CHECK(std::filesystem::file_size(f.destination / "Build" / "FortniteGame" / "Binaries" / "Win64" /
                                         "FortniteClient-Win64-Shipping.exe") == 70000);
        CHECK(std::filesystem::is_regular_file(f.destination / "Build" / "empty.txt"));
        REQUIRE_FALSE(f.progress.empty());
        CHECK(f.progress.back().bytes_done == 70000 + 5);
        CHECK(f.progress.back().entries_done == 4);
        if (shape == "zip") CHECK(f.progress.back().bytes_total == u64{70000 + 5});
        if (shape == "wrapped") CHECK(summary->probe.container == catalog::ArchiveContainer::ZipStored);
    }
}

TEST_CASE("backslashes are folders, and names differing only in case land on one file") {
    Fixture f;
    auto summary = f.extract(make_archive("zip", {{"Dir\\File.txt", "first"}, {"dir/file.TXT", "second"}}));
    REQUIRE(summary);
    CHECK(f.text(NativePath("Dir") / "File.txt") == "second");
    REQUIRE(summary->case_collisions.size() == 1);
    CHECK(summary->case_collisions[0].kept == "dir/file.TXT");
    CHECK(summary->case_collisions[0].replaced == "Dir/File.txt");
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(f.destination))
        if (entry.is_regular_file()) ++files;
    CHECK(files == 1);
}

TEST_CASE("a collision only in a folder's case is reported too") {
    Fixture f;
    auto summary = f.extract(make_archive("zip", {{"Pak/a.pak", "first"}, {"pak/a.pak", "second"}}));
    REQUIRE(summary);
    CHECK(f.text(NativePath("Pak") / "a.pak") == "second");
    REQUIRE(summary->case_collisions.size() == 1);
    CHECK(summary->case_collisions[0].kept == "pak/a.pak");
    CHECK(summary->case_collisions[0].replaced == "Pak/a.pak");
}

TEST_CASE("entries that would leave the destination are refused") {
    for (const std::string name : {"../evil.txt", "a/../../evil.txt", "/abs.txt", "C:/drive.txt", "a/file.txt:stream",
                                   "a/CON", "a/nul.txt", "a/Lpt1 .log"}) {
        INFO(name);
        Fixture f;
        auto summary = f.extract(make_archive("zip", {{name, "x"}}));
        REQUIRE_FALSE(summary);
        CHECK(summary.error().id == "builds.unsafe_entry_path");
        CHECK(arg_text(summary.error(), "archive_entry") == name);
        CHECK_FALSE(std::filesystem::exists(f.dir.path() / "evil.txt"));
    }

    Fixture f;
    auto link = f.extract(make_archive("zip", {{"link", "../../etc", false, true}}));
    REQUIRE_FALSE(link);
    CHECK(link.error().id == "builds.unsafe_entry_path");
}

TEST_CASE("extraction fails cleanly on bad input and stops when cancelled") {
    Fixture f;
    auto unsupported = f.extract(std::vector<u8>(256, 0x42));
    REQUIRE_FALSE(unsupported);
    CHECK(unsupported.error().id == "builds.unsupported_archive");
    CHECK(arg_text(unsupported.error(), "path") == display_utf8(f.archive));

    std::vector<u8> truncated = make_archive("7zip", kBuild);
    truncated.resize(truncated.size() / 2);
    auto corrupt = f.extract(truncated);
    REQUIRE_FALSE(corrupt);
    CHECK((corrupt.error().id == "builds.corrupt_archive" || corrupt.error().id == "builds.unsupported_archive"));

    CancelSource cancel;
    cancel.cancel(CancelReason::User);
    auto cancelled = f.extract(make_archive("zip", kBuild), cancel.token());
    REQUIRE_FALSE(cancelled);
    CHECK(cancelled.error().id == "builds.cancelled");
}

// ScratchDir::create tries 8 names; a fixed seed would offer the same 8 on every call.
TEST_CASE("scratch directories never repeat a name") {
    std::vector<testing::ScratchDir> dirs;
    for (int i = 0; i < 9; ++i) dirs.push_back(make_scratch("reboot-builds-scratch"));
    CHECK(dirs.front().path() != dirs.back().path());
}
