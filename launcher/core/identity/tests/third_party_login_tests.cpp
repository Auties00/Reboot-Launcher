#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "reboot/identity/third_party_login.hpp"
#include "reboot/testing/golden.hpp"

using namespace rb;
using namespace rb::identity;
namespace json = boost::json;

TEST_CASE("third_party_login matches its golden vectors", "[identity]") {
    const Result<std::vector<u8>> bytes = testing::read_golden(NativePath(REBOOT_IDENTITY_TEST_DATA) /
                                                               "third_party_login.json");
    REQUIRE(bytes);
    const json::value golden = json::parse(std::string(bytes->begin(), bytes->end()));
    const json::array& cases = golden.at("cases").as_array();
    REQUIRE_FALSE(cases.empty());
    for (const json::value& item : cases) {
        const json::object& entry = item.as_object();
        const std::string name(entry.at("name").as_string());
        INFO("name: " << name);
        CHECK(third_party_login(name) == std::string_view(entry.at("auth_login").as_string()));
    }
}
