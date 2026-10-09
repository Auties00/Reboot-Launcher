#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "pem.hpp"

using rb::u8;
using rb::os_macos::platform::pem_bundle;

TEST_CASE("pem_bundle wraps each certificate in its own block", "[pem]") {
    const std::vector<std::vector<u8>> certificates{{'M', 'a', 'n'}, {'M', 'a'}, {'M'}};
    CHECK(pem_bundle(certificates) ==
          "-----BEGIN CERTIFICATE-----\nTWFu\n-----END CERTIFICATE-----\n"
          "-----BEGIN CERTIFICATE-----\nTWE=\n-----END CERTIFICATE-----\n"
          "-----BEGIN CERTIFICATE-----\nTQ==\n-----END CERTIFICATE-----\n");
}

TEST_CASE("pem_bundle breaks base64 into 64-column lines", "[pem]") {
    const std::vector<std::vector<u8>> certificates{std::vector<u8>(60, 0xFF)};
    const std::string pem = pem_bundle(certificates);
    const std::string body = pem.substr(pem.find('\n') + 1);
    CHECK(body.find('\n') == 64);
    CHECK(body.substr(0, 64) == std::string(64, '/'));
    CHECK(body.substr(65, 16) == std::string(16, '/'));
}

TEST_CASE("an empty set of certificates is an empty bundle", "[pem]") {
    CHECK(pem_bundle({}).empty());
}
