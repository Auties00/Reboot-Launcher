#pragma once

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/types.hpp"

// Last: on Windows it brings in windows.h and its min and max macros.
#include <archive.h>
#include <archive_entry.h>

namespace reboot::builds::test {

struct Entry {
    std::string name;
    std::string data;
    bool directory = false;
    bool symlink = false;
};

inline la_ssize_t append(archive*, void* out, const void* buffer, std::size_t length) {
    auto& bytes = *static_cast<std::vector<u8>*>(out);
    const auto* begin = static_cast<const u8*>(buffer);
    bytes.insert(bytes.end(), begin, begin + length);
    return static_cast<la_ssize_t>(length);
}

// An archive written by libarchive itself: "zip" stored or deflated, or "7zip" with LZMA2.
inline std::vector<u8> make_archive(std::string_view format, const std::vector<Entry>& entries, bool store = false) {
    std::vector<u8> bytes;
    archive* writer = archive_write_new();
    REQUIRE(writer != nullptr);
    if (format == "zip") {
        REQUIRE(archive_write_set_format_zip(writer) == ARCHIVE_OK);
        REQUIRE(archive_write_set_options(writer, store ? "zip:compression=store" : "zip:compression=deflate") ==
                ARCHIVE_OK);
    } else {
        REQUIRE(archive_write_set_format_7zip(writer) == ARCHIVE_OK);
        REQUIRE(archive_write_set_options(writer, "7zip:compression=lzma2") == ARCHIVE_OK);
    }
    REQUIRE(archive_write_set_bytes_in_last_block(writer, 1) == ARCHIVE_OK);
    REQUIRE(archive_write_open2(writer, &bytes, nullptr, append, nullptr, nullptr) == ARCHIVE_OK);
    for (const Entry& entry : entries) {
        archive_entry* header = archive_entry_new();
        archive_entry_set_pathname(header, entry.name.c_str());
        if (entry.directory) {
            archive_entry_set_filetype(header, AE_IFDIR);
            archive_entry_set_perm(header, 0755);
        } else if (entry.symlink) {
            archive_entry_set_filetype(header, AE_IFLNK);
            archive_entry_set_symlink(header, entry.data.c_str());
            archive_entry_set_perm(header, 0777);
        } else {
            archive_entry_set_filetype(header, AE_IFREG);
            archive_entry_set_perm(header, 0644);
            archive_entry_set_size(header, static_cast<la_int64_t>(entry.data.size()));
        }
        REQUIRE(archive_write_header(writer, header) == ARCHIVE_OK);
        if (!entry.directory && !entry.symlink && !entry.data.empty())
            REQUIRE(archive_write_data(writer, entry.data.data(), entry.data.size()) ==
                    static_cast<la_ssize_t>(entry.data.size()));
        archive_entry_free(header);
    }
    REQUIRE(archive_write_close(writer) == ARCHIVE_OK);
    archive_write_free(writer);
    return bytes;
}

}  // namespace reboot::builds::test
