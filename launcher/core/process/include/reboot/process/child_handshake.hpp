#pragma once

#include <expected>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::process {

// The role-specific half of a stdio handshake (BackendHello/BackendWelcome, ServerHello/ServerWelcome).
// ChildChannel compares the protocol itself, so every role gets the same exact-match rule.
struct ChildHandshake {
    u64 hello_frame = 0;
    u32 expected_protocol = 0;
    // Decodes the Hello far enough to read its protocol integer.
    UniqueFunction<Result<u32>(std::span<const u8> payload)> read_protocol;
    // Runs once the protocol matched, on every spawn: checks the rest of the Hello and returns
    // the encoded Welcome frame.
    UniqueFunction<Result<std::vector<u8>>(std::span<const u8> payload)> welcome;
};

// `protocol_of(hello)` reads the protocol; `welcome(hello)` returns Result<WelcomeMessage>.
template <ContractMessage Hello, class ProtocolOf, class Welcome>
    requires std::is_invocable_r_v<u32, ProtocolOf&, const Hello&> &&
             ContractMessage<typename std::invoke_result_t<Welcome&, const Hello&>::value_type>
[[nodiscard]] ChildHandshake make_child_handshake(u32 expected_protocol, ProtocolOf protocol_of, Welcome welcome) {
    ChildHandshake handshake;
    handshake.hello_frame = contract_frame_type_v<Hello>;
    handshake.expected_protocol = expected_protocol;
    handshake.read_protocol = [protocol_of = std::move(protocol_of)](std::span<const u8> payload) mutable -> Result<u32> {
        Result<Hello> hello = decode_contract<Hello>(payload);
        if (!hello) return std::unexpected(std::move(hello.error()));
        return protocol_of(*hello);
    };
    handshake.welcome = [welcome = std::move(welcome)](std::span<const u8> payload) mutable -> Result<std::vector<u8>> {
        Result<Hello> hello = decode_contract<Hello>(payload);
        if (!hello) return std::unexpected(std::move(hello.error()));
        auto reply = welcome(*hello);
        if (!reply) return std::unexpected(std::move(reply.error()));
        return encode_contract_frame(*reply);
    };
    return handshake;
}

}  // namespace rb::process
