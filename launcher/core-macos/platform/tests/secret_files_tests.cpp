#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "reboot/testing/in_memory_file_system.hpp"
#include "secret_files.hpp"

using namespace reboot::os_macos::platform;
using reboot::NativePath;
using reboot::u8;

namespace {

const NativePath kDir{"/data/state/secrets"};

}  // namespace

TEST_CASE("a secret file round-trips binary values", "[secret_files]") {
    reboot::testing::InMemoryFileSystem fs;
    SecretFiles files(kDir, fs);
    const std::vector<u8> value{0x00, 'p', 0xFF, 0x00};
    REQUIRE(files.put("account/1/password", value));
    auto read = files.get("account/1/password");
    REQUIRE(read);
    REQUIRE(read->has_value());
    CHECK((*read)->reveal() == value);
}

TEST_CASE("a missing secret file is nullopt and erasing it succeeds", "[secret_files]") {
    reboot::testing::InMemoryFileSystem fs;
    SecretFiles files(kDir, fs);
    auto read = files.get("never-written");
    REQUIRE(read);
    CHECK_FALSE(read->has_value());
    CHECK(files.erase("never-written"));
}

TEST_CASE("erase removes the file and put overwrites it", "[secret_files]") {
    reboot::testing::InMemoryFileSystem fs;
    SecretFiles files(kDir, fs);
    REQUIRE(files.put("k", std::vector<u8>{1}));
    REQUIRE(files.put("k", std::vector<u8>{2, 3}));
    auto read = files.get("k");
    REQUIRE(read);
    REQUIRE(read->has_value());
    CHECK((*read)->reveal() == std::vector<u8>{2, 3});
    REQUIRE(files.erase("k"));
    auto erased = files.get("k");
    REQUIRE(erased);
    CHECK_FALSE(erased->has_value());
}

TEST_CASE("file names are bounded hashes inside the directory", "[secret_files]") {
    reboot::testing::InMemoryFileSystem fs;
    SecretFiles files(kDir, fs);
    const NativePath path = files.path_of(std::string(1000, '/'));
    CHECK(path.parent_path() == kDir);
    CHECK(path.filename().string().size() == 64 + std::string_view(".secret").size());
    CHECK(files.path_of("a") != files.path_of("b"));
}
