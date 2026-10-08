#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <iterator>
#include <string>

#include "reboot/identity/legacy_login.hpp"

using namespace reboot::identity;
namespace json = boost::json;

namespace {

json::value read_golden(const char* name) {
    std::ifstream stream(std::string(REBOOT_IDENTITY_TEST_DATA) + "/" + name, std::ios::binary);
    REQUIRE(stream);
    const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    return json::parse(text);
}

}  // namespace

TEST_CASE("legacy_derive matches the 10.0.9 goldens", "[identity]") {
    const json::value golden = read_golden("legacy_derive.json");
    const json::array& cases = golden.at("cases").as_array();
    REQUIRE_FALSE(cases.empty());
    for (const json::value& item : cases) {
        const json::object& entry = item.as_object();
        const std::string username(entry.at("username").as_string());
        const bool has_password = entry.at("has_password").as_bool();
        const LegacyPassword password = entry.at("password").as_string() == "user_password"
                                            ? LegacyPassword::UserPassword
                                            : LegacyPassword::Placeholder;
        INFO("username: " << username << ", has_password: " << has_password);
        CHECK(legacy_derive(username, has_password) ==
              LegacyLogin{std::string(entry.at("auth_login").as_string()), password});
    }
}
