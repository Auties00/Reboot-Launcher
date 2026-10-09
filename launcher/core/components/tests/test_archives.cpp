#include "test_archives.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <catch2/catch_test_macros.hpp>
#include <memory>

namespace rb::components::test {

namespace {

struct ArchiveDeleter {
    void operator()(archive* handle) const noexcept { archive_write_free(handle); }
};

}  // namespace

std::string make_tar_gz(const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<char> buffer(1 << 20);
    std::size_t used = 0;
    const std::unique_ptr<archive, ArchiveDeleter> out(archive_write_new());
    REQUIRE(archive_write_add_filter_gzip(out.get()) == ARCHIVE_OK);
    REQUIRE(archive_write_set_format_pax_restricted(out.get()) == ARCHIVE_OK);
    REQUIRE(archive_write_open_memory(out.get(), buffer.data(), buffer.size(), &used) == ARCHIVE_OK);
    for (const auto& [name, content] : files) {
        archive_entry* entry = archive_entry_new();
        archive_entry_set_pathname(entry, name.c_str());
        archive_entry_set_size(entry, static_cast<la_int64_t>(content.size()));
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0755);
        REQUIRE(archive_write_header(out.get(), entry) == ARCHIVE_OK);
        REQUIRE(archive_write_data(out.get(), content.data(), content.size()) == static_cast<la_ssize_t>(content.size()));
        archive_entry_free(entry);
    }
    REQUIRE(archive_write_close(out.get()) == ARCHIVE_OK);
    return {buffer.data(), used};
}

}  // namespace rb::components::test
