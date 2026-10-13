#pragma once

#include <catch2/catch_test_macros.hpp>

#include <archive.h>
#include <archive_entry.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <vector>

#include "reboot/base/native_path.hpp"

// Writes update packages for the update conformance cases.
namespace rb::os_linux::platform::test {

struct Item {
    std::string path;
    mode_t type = S_IFREG;
    mode_t perm = 0644;
    std::string content;
    std::string link;
    bool hardlink = false;
};

// A zstd-compressed tar, as release builds publish it.
inline void write_package(const NativePath& file, const std::vector<Item>& items) {
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

[[nodiscard]] inline std::vector<Item> release(std::string_view version) {
    const std::string top{version};
    return {
        {.path = "./" + top + "/", .type = S_IFDIR, .perm = 0755},
        {.path = "./" + top + "/bin/reboot-engine", .perm = 04755, .content = "#!/bin/sh\nexec true\n"},
        {.path = "./" + top + "/lib/libx.so.1", .content = "elf"},
        {.path = "./" + top + "/lib/libx.so", .type = S_IFLNK, .perm = 0777, .link = "libx.so.1"},
        {.path = "./" + top + "/lib/libx.copy", .link = "./" + top + "/lib/libx.so.1", .hardlink = true},
    };
}

}  // namespace rb::os_linux::platform::test
