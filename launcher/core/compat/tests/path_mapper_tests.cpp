#include <catch2/catch_test_macros.hpp>

#include <string>

#include "messages.hpp"
#include "reboot/compat/path_mapper.hpp"

// PathMapper serves Wine prefixes, which exist only on macOS and Linux.
#if !defined(_WIN32)

using namespace rb;
using namespace rb::compat;

namespace {

PathMapper sample_mapper() {
    return PathMapper({{'c', "/home/user/.local/share/reboot/prefixes/umu/drive_c"}, {'z', "/"}, {'d', "/mnt/games"}});
}

}  // namespace

TEST_CASE("the longest drive target wins", "[compat][paths]") {
    const PathMapper mapper = sample_mapper();
    CHECK(mapper.to_windows("/mnt/games/Fortnite/FortniteGame") == std::u16string(u"D:\\Fortnite\\FortniteGame"));
    CHECK(mapper.to_windows("/home/user/.local/share/reboot/prefixes/umu/drive_c/windows") ==
          std::u16string(u"C:\\windows"));
    CHECK(mapper.to_windows("/opt/build") == std::u16string(u"Z:\\opt\\build"));
}

TEST_CASE("a drive root maps to its letter", "[compat][paths]") {
    CHECK(sample_mapper().to_windows("/mnt/games") == std::u16string(u"D:\\"));
}

TEST_CASE("no Z: is assumed", "[compat][paths]") {
    const PathMapper mapper({{'d', "/mnt/games"}});
    const auto windows = mapper.to_windows("/opt/build");
    REQUIRE_FALSE(windows);
    CHECK(windows.error().is(msg::kPathNotMapped));
}

TEST_CASE("names Windows cannot spell are refused", "[compat][paths]") {
    const auto windows = sample_mapper().to_windows("/mnt/games/a:b");
    REQUIRE_FALSE(windows);
    CHECK(windows.error().is(msg::kPathNotUtf8));
}

TEST_CASE("relative host paths are refused", "[compat][paths]") {
    CHECK_FALSE(sample_mapper().to_windows("games/build"));
}

#endif
