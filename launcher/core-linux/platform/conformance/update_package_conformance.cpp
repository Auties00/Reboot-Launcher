#include <catch2/catch_test_macros.hpp>

#include <archive.h>
#include <archive_entry.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "conformance_support.hpp"
#include "messages.hpp"
#include "update_package.hpp"

using namespace rb;
using namespace rb::os_linux::platform;
using rb::os_linux::platform::test::Scratch;
namespace fs = std::filesystem;

namespace {

struct Item {
    std::string path;
    mode_t type = S_IFREG;
    mode_t perm = 0644;
    std::string content;
    std::string link;
    bool hardlink = false;
};

// A zstd-compressed tar, as release builds publish it.
void write_package(const NativePath& file, const std::vector<Item>& items) {
    archive* const writer = ::archive_write_new();
    REQUIRE(::archive_write_add_filter_zstd(writer) == ARCHIVE_OK);
    REQUIRE(::archive_write_set_format_pax_restricted(writer) == ARCHIVE_OK);
    REQUIRE(::archive_write_open_filename(writer, file.c_str()) == ARCHIVE_OK);
    for (const Item& item : items) {
        archive_entry* const entry = ::archive_entry_new();
        ::archive_entry_set_pathname(entry, item.path.c_str());
        ::archive_entry_set_filetype(entry, item.type);
        ::archive_entry_set_perm(entry, item.perm);
        if (item.hardlink) {
            ::archive_entry_set_hardlink(entry, item.link.c_str());
        } else if (item.type == S_IFLNK) {
            ::archive_entry_set_symlink(entry, item.link.c_str());
        }
        if (item.type == S_IFREG && !item.hardlink) ::archive_entry_set_size(entry, static_cast<la_int64_t>(item.content.size()));
        REQUIRE(::archive_write_header(writer, entry) == ARCHIVE_OK);
        if (!item.content.empty() && !item.hardlink)
            REQUIRE(::archive_write_data(writer, item.content.data(), item.content.size()) ==
                    static_cast<la_ssize_t>(item.content.size()));
        ::archive_entry_free(entry);
    }
    REQUIRE(::archive_write_close(writer) == ARCHIVE_OK);
    ::archive_write_free(writer);
}

[[nodiscard]] std::vector<Item> release(std::string_view version) {
    const std::string top{version};
    return {
        {.path = "./" + top + "/", .type = S_IFDIR, .perm = 0755},
        {.path = "./" + top + "/bin/reboot-engine", .perm = 04755, .content = "#!/bin/sh\nexec true\n"},
        {.path = "./" + top + "/lib/libx.so.1", .content = "elf"},
        {.path = "./" + top + "/lib/libx.so", .type = S_IFLNK, .perm = 0777, .link = "libx.so.1"},
        {.path = "./" + top + "/lib/libx.copy", .link = "./" + top + "/lib/libx.so.1", .hardlink = true},
    };
}

[[nodiscard]] Result<std::string> extract(Scratch& scratch, const std::vector<Item>& items) {
    const NativePath package = scratch.dir.path() / "update.tar.zst";
    write_package(package, items);
    const NativePath staging = scratch.dir.path() / "staging";
    fs::remove_all(staging);
    fs::create_directory(staging);
    return extract_update(package, staging);
}

void require_unsafe(const Result<std::string>& extracted) {
    REQUIRE_FALSE(extracted);
    CHECK(extracted.error().is(kUpdateEntryUnsafe));
}

}  // namespace

TEST_CASE("a release extracts its one version directory without set-id bits", "[linux_conformance]") {
    Scratch scratch;
    const Result<std::string> version = extract(scratch, release("11.2.0"));
    REQUIRE(version);
    CHECK(*version == "11.2.0");
    const NativePath root = scratch.dir.path() / "staging" / "11.2.0";
    struct stat info {};
    REQUIRE(::stat((root / "bin" / "reboot-engine").c_str(), &info) == 0);
    CHECK((info.st_mode & 07777) == 0755);
    CHECK(fs::read_symlink(root / "lib" / "libx.so") == NativePath{"libx.so.1"});
    std::ifstream copy(root / "lib" / "libx.copy");
    CHECK(std::string(std::istreambuf_iterator<char>(copy), {}) == "elf");
}

TEST_CASE("a package with more than one top-level entry is invalid", "[linux_conformance]") {
    Scratch scratch;
    std::vector<Item> items = release("11.2.0");
    items.push_back({.path = "README", .content = "x"});
    const Result<std::string> extracted = extract(scratch, items);
    REQUIRE_FALSE(extracted);
    CHECK(extracted.error().is(kUpdatePackageInvalid));

    const Result<std::string> only_file = extract(scratch, {{.path = "11.2.0", .content = "x"}});
    REQUIRE_FALSE(only_file);
    CHECK(only_file.error().is(kUpdatePackageInvalid));
}

TEST_CASE("absolute and parent-relative entries are refused", "[linux_conformance]") {
    Scratch scratch;
    std::vector<Item> absolute = release("11.2.0");
    absolute.push_back({.path = "/tmp/reboot-evil", .content = "x"});
    require_unsafe(extract(scratch, absolute));
    std::vector<Item> parent = release("11.2.0");
    parent.push_back({.path = "11.2.0/../../reboot-evil", .content = "x"});
    require_unsafe(extract(scratch, parent));
    CHECK_FALSE(fs::exists(scratch.dir.path() / "reboot-evil"));
}

TEST_CASE("links leaving the staging directory are refused, even through a chain", "[linux_conformance]") {
    Scratch scratch;
    std::vector<Item> direct = release("11.2.0");
    direct.push_back({.path = "11.2.0/escape", .type = S_IFLNK, .perm = 0777, .link = "../../../etc"});
    require_unsafe(extract(scratch, direct));

    std::vector<Item> absolute = release("11.2.0");
    absolute.push_back({.path = "11.2.0/etc", .type = S_IFLNK, .perm = 0777, .link = "/etc"});
    require_unsafe(extract(scratch, absolute));

    std::vector<Item> chained = release("11.2.0");
    chained.push_back({.path = "11.2.0/here", .type = S_IFLNK, .perm = 0777, .link = "."});
    chained.push_back({.path = "11.2.0/away", .type = S_IFLNK, .perm = 0777, .link = "here/../.."});
    require_unsafe(extract(scratch, chained));

    // Dangling at the end of the chain, so only resolving the link's target catches it.
    std::vector<Item> dangling = release("11.2.0");
    dangling.push_back({.path = "11.2.0/here", .type = S_IFLNK, .perm = 0777, .link = "."});
    dangling.push_back({.path = "11.2.0/later", .type = S_IFLNK, .perm = 0777, .link = "here/../../reboot-missing"});
    require_unsafe(extract(scratch, dangling));
}

TEST_CASE("devices, FIFOs and sockets are refused", "[linux_conformance]") {
    Scratch scratch;
    for (const mode_t type : {mode_t{S_IFCHR}, mode_t{S_IFBLK}, mode_t{S_IFIFO}, mode_t{S_IFSOCK}}) {
        std::vector<Item> items = release("11.2.0");
        items.push_back({.path = "11.2.0/node", .type = type, .perm = 0600});
        require_unsafe(extract(scratch, items));
    }
}

TEST_CASE("a missing package fails", "[linux_conformance]") {
    Scratch scratch;
    fs::create_directory(scratch.dir.path() / "staging");
    CHECK_FALSE(extract_update(scratch.dir.path() / "missing.tar.zst", scratch.dir.path() / "staging"));
}
