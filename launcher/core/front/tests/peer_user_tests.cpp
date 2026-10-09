#include <catch2/catch_test_macros.hpp>
#include <optional>

#include "reboot/front/peer_user.hpp"

using namespace rb;
using namespace rb::front;

namespace {

Result<std::optional<u32>> failure(ErrorKind kind) {
    return make_diag(ErrorDomain::Platform, MessageId{"platform.not_supported"}).kind(kind).fail();
}

}  // namespace

TEST_CASE("an unsupported inspector leaves the peer unchecked", "[front][peer]") {
    CHECK(classify_peer(failure(ErrorKind::Unsupported), 1000) == PeerUser::Unchecked);
    CHECK(classify_peer(failure(ErrorKind::Unsupported), std::nullopt) == PeerUser::Unchecked);
}

TEST_CASE("the peer check fails closed", "[front][peer]") {
    CHECK(classify_peer(failure(ErrorKind::Generic), 1000) == PeerUser::Other);
    CHECK(classify_peer(std::optional<u32>{}, 1000) == PeerUser::Other);
    CHECK(classify_peer(std::optional<u32>{1000}, std::nullopt) == PeerUser::Other);
    CHECK(classify_peer(std::optional<u32>{1001}, 1000) == PeerUser::Other);
}

TEST_CASE("the engine's own uid is the engine", "[front][peer]") {
    CHECK(classify_peer(std::optional<u32>{1000}, 1000) == PeerUser::Engine);
}
