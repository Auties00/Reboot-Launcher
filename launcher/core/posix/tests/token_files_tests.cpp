#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <sys/stat.h>

#include "exclusive_file.hpp"
#include "messages.hpp"
#include "posix_test_support.hpp"
#include "reboot/posix/token_files.hpp"

using namespace rb;
using namespace rb::posix;

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kToken = "dG9rZW4tdGV4dC10aGF0LWlzLTQzLWNoYXJzLWxvbmctQQ";

[[nodiscard]] u32 mode_of(const NativePath& path) {
    struct stat info {};
    REQUIRE(::lstat(path.c_str(), &info) == 0);
    return static_cast<u32>(info.st_mode) & 07777U;
}

[[nodiscard]] std::string read_all(const NativePath& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::size_t entries(const NativePath& dir) {
    return static_cast<std::size_t>(std::distance(fs::directory_iterator(dir), fs::directory_iterator()));
}

// The process umask for one scope.
class Umask {
public:
    explicit Umask(::mode_t mask) : previous_(::umask(mask)) {}
    ~Umask() { ::umask(previous_); }
    Umask(const Umask&) = delete;
    Umask& operator=(const Umask&) = delete;

private:
    ::mode_t previous_;
};

}  // namespace

TEST_CASE("a token file is 0600 in a fresh 0700 directory under a base created 0700") {
    const testing::ScratchDir scratch = test::make_scratch("token-files");
    const NativePath base = scratch.path() / "tokens";
    const TokenFiles files{base};

    auto file = files.write(kToken);
    REQUIRE(file);
    const NativePath path = (*file)->path();
    CHECK(path == fs::canonical(path));
    CHECK(path.filename() == "token");
    CHECK(path.parent_path().filename().string().starts_with("ctl-"));
    CHECK(path.parent_path().parent_path() == fs::canonical(base));
    CHECK(mode_of(base) == 0700U);
    CHECK(mode_of(path.parent_path()) == 0700U);
    CHECK(mode_of(path) == 0600U);
    CHECK(read_all(path) == kToken);

    auto second = files.write(kToken);
    REQUIRE(second);
    CHECK((*second)->path().parent_path() != path.parent_path());

    file->reset();
    CHECK_FALSE(fs::exists(path));
    CHECK_FALSE(fs::exists(path.parent_path()));
    CHECK(fs::exists((*second)->path()));
    second->reset();
    CHECK(fs::is_directory(base));
    CHECK(entries(base) == 0);
}

TEST_CASE("the token file keeps 0600 under any umask") {
    const testing::ScratchDir scratch = test::make_scratch("token-files");
    for (const ::mode_t mask : {::mode_t{0}, ::mode_t{0277}}) {
        const Umask umask{mask};
        auto file = TokenFiles{scratch.path() / "tokens"}.write(kToken);
        REQUIRE(file);
        CHECK(mode_of((*file)->path()) == 0600U);
        CHECK(mode_of((*file)->path().parent_path()) == 0700U);
    }
}

TEST_CASE("a base that is not this user's alone is refused and nothing is written") {
    const testing::ScratchDir scratch = test::make_scratch("token-files");
    const NativePath base = scratch.path() / "tokens";

    SECTION("group- or world-accessible") {
        REQUIRE(::mkdir(base.c_str(), 0700) == 0);
        REQUIRE(::chmod(base.c_str(), 0755) == 0);
        const auto refused = TokenFiles{base}.write(kToken);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().is(kDirectoryNotPrivate));
        CHECK(entries(base) == 0);
    }
    SECTION("a link to a private directory") {
        const NativePath target = scratch.path() / "elsewhere";
        REQUIRE(::mkdir(target.c_str(), 0700) == 0);
        fs::create_directory_symlink(target, base);
        const auto refused = TokenFiles{base}.write(kToken);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().is(kNotADirectory));
        CHECK(entries(target) == 0);
    }
    SECTION("a file") {
        std::ofstream{base} << "not a directory";
        const auto refused = TokenFiles{base}.write(kToken);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().is(kNotADirectory));
    }
}

TEST_CASE("an exclusive file refuses an existing file and a planted link") {
    const testing::ScratchDir scratch = test::make_scratch("token-files");
    const NativePath path = scratch.path() / "token";
    REQUIRE(write_exclusive_file(path, kToken));
    CHECK(read_all(path) == kToken);

    const auto again = write_exclusive_file(path, "other");
    REQUIRE_FALSE(again);
    CHECK(again.error().is(kCallFailedOnPath));
    CHECK(read_all(path) == kToken);

    const NativePath target = scratch.path() / "target";
    std::ofstream{target} << "kept";
    const NativePath link = scratch.path() / "link";
    fs::create_symlink(target, link);
    REQUIRE_FALSE(write_exclusive_file(link, kToken));
    CHECK(read_all(target) == "kept");
    const NativePath dangling = scratch.path() / "dangling";
    fs::create_symlink(scratch.path() / "missing", dangling);
    REQUIRE_FALSE(write_exclusive_file(dangling, kToken));
    CHECK_FALSE(fs::exists(scratch.path() / "missing"));
}

TEST_CASE("a handle whose file is already gone still removes its directory") {
    const testing::ScratchDir scratch = test::make_scratch("token-files");
    auto file = TokenFiles{scratch.path() / "tokens"}.write(kToken);
    REQUIRE(file);
    const NativePath path = (*file)->path();
    REQUIRE(fs::remove(path));
    file->reset();
    CHECK_FALSE(fs::exists(path.parent_path()));
}

TEST_CASE("a sweep removes what an earlier engine left and keeps the base") {
    const testing::ScratchDir scratch = test::make_scratch("token-files");
    const NativePath base = scratch.path() / "tokens";
    const TokenFiles files{base};

    SECTION("a missing base stays missing") {
        files.sweep();
        CHECK_FALSE(fs::exists(base));
    }
    SECTION("stale ctl- directories, files and links go; a link's target and other entries stay") {
        auto left = files.write(kToken);
        REQUIRE(left);
        const NativePath outside = scratch.path() / "outside";
        std::ofstream{outside} << "kept";
        fs::create_symlink(outside, base / "ctl-link");
        std::ofstream{base / "ctl-stray"} << "stray";
        std::ofstream{base / "other.sock"} << "not ours to sweep";
        files.sweep();
        CHECK(fs::is_directory(base));
        CHECK(mode_of(base) == 0700U);
        CHECK(entries(base) == 1);
        CHECK(fs::exists(base / "other.sock"));
        CHECK(read_all(outside) == "kept");
        // The handle of a token swept from under it removes nothing else.
        left->reset();
        CHECK(entries(base) == 1);
    }
    SECTION("a base that is not this user's alone is left alone") {
        REQUIRE(::mkdir(base.c_str(), 0700) == 0);
        std::ofstream{base / "ctl-stray"} << "stray";
        REQUIRE(::chmod(base.c_str(), 0755) == 0);
        files.sweep();
        CHECK(fs::exists(base / "ctl-stray"));
    }
}
