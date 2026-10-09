#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/types.hpp"
#include "reboot/secrets/secret_target.hpp"
#include "store_layout.hpp"

using namespace rb;
using rb::secrets::SecretKind;
using rb::secrets::SecretTarget;

namespace {

std::vector<u8> bytes(std::string_view text) { return {text.begin(), text.end()}; }

}  // namespace

TEST_CASE("store keys are namespaced by the data root", "[secrets][layout]") {
    const rb::secrets::detail::StoreLayout layout("00112233aabbccdd");
    CHECK(layout.index_key() == "00112233aabbccdd/index");
    const SecretTarget target = SecretTarget::parse(SecretKind::RemoteBackendPassword, "Backend:3551").value();
    CHECK(layout.value_key(target) == "00112233aabbccdd/remote-backend-password/backend:3551");
}

TEST_CASE("the index round-trips the stored targets", "[secrets][layout]") {
    const std::set<SecretTarget> targets{
        SecretTarget::parse(SecretKind::RemoteBackendPassword, "[fe80::1]:443").value(),
        SecretTarget::parse(SecretKind::HostJoinPassword, "0a1b2c3d-4e5f-6071-8293-a4b5c6d7e8f9").value(),
    };
    const std::vector<u8> encoded = rb::secrets::detail::encode_index(targets);
    CHECK(std::string(encoded.begin(), encoded.end()) ==
          "remote-backend-password/[fe80::1]:443\nhost-join-password/0a1b2c3d-4e5f-6071-8293-a4b5c6d7e8f9\n");
    CHECK(rb::secrets::detail::decode_index(encoded) == targets);
    CHECK(rb::secrets::detail::encode_index({}).empty());
}

TEST_CASE("index lines that name no storable target are skipped", "[secrets][layout]") {
    const std::set<SecretTarget> decoded = rb::secrets::detail::decode_index(
        bytes("join-password/42\nno-slash\nunknown-kind/x\nhost-join-password/not-a-uuid\n"
              "remote-backend-password/backend.example.com"));
    REQUIRE(decoded.size() == 1);
    CHECK(decoded.begin()->scope.text() == "backend.example.com");
}
