#include <cstddef>
#include <cstdint>
#include <span>

#include "reboot/api/codec.hpp"
#include "reboot/api/v1/common.hpp"
#include "reboot/api/v1/host.hpp"
#include "reboot/api/v1/join.hpp"
#include "reboot/api/v1/play.hpp"
#include "reboot/api/v1/requests.hpp"
#include "reboot/api/v1/secrets.hpp"
#include "reboot/api/v1/settings.hpp"
#include "reboot/api/v1/support.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/testing/fuzz.hpp"

using reboot::testing::fuzz_require;

namespace {

// What decodes must encode to bytes that decode again.
template <class Message>
void decode_and_reencode(std::span<const reboot::u8> input) {
    const auto message = reboot::api::decode<Message>(input);
    if (!message) return;
    const reboot::api::Bytes again = reboot::api::encode(*message);
    fuzz_require(reboot::api::decode<Message>(again).has_value(), "a decoded payload does not survive re-encoding");
}

}  // namespace

// Call and Start payloads and Subscribe filters a client sends; the ones holding a oneof also run
// their case checks.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const reboot::u8> input(data, size);
    decode_and_reencode<reboot::api::EventFilter>(input);
    decode_and_reencode<reboot::api::HostProfilesCreateRequest>(input);
    decode_and_reencode<reboot::api::HostProfilesUpdateRequest>(input);
    decode_and_reencode<reboot::api::HostCommandRequest>(input);
    decode_and_reencode<reboot::api::JoinSetCustomTargetRequest>(input);
    decode_and_reencode<reboot::api::PlayPlanRequest>(input);
    decode_and_reencode<reboot::api::PlayStartRequest>(input);
    decode_and_reencode<reboot::api::RequestsRespondRequest>(input);
    decode_and_reencode<reboot::api::SecretsStateRequest>(input);
    decode_and_reencode<reboot::api::SecretsClearRequest>(input);
    decode_and_reencode<reboot::api::SettingsPatchRequest>(input);
    decode_and_reencode<reboot::api::SupportQueryRequest>(input);
    return 0;
}
