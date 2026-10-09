#pragma once

#include <optional>
#include <span>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ipc/connection_info.hpp"

namespace rb::ipc {

// Covers no capability ids. The engine's ApiRouter; only it decodes reboot.api.v1. Strand-only.
class IApiDispatcher {
public:
    virtual ~IApiDispatcher() = default;

    virtual void on_connected(const ConnectionInfo& connection) = 0;
    // Runs after OpRegistry::on_connection_closed; ends the connection's Client leases.
    virtual void on_disconnected(ConnectionId connection) = 0;

    // Only methods allows_method() passed get here; an error becomes Reply.error.
    virtual Result<contracts::ipc::Bytes> call(const ConnectionInfo& from, u32 method_id,
                                               std::span<const u8> request) = 0;
    // No `disconnect` means the method's default.
    virtual Result<OpHandle> start(const ConnectionInfo& from, u32 method_id, std::span<const u8> request,
                                   std::optional<DisconnectPolicy> disconnect) = 0;

    // An encoded reboot.api.v1 Outcome.
    [[nodiscard]] virtual contracts::ipc::Bytes encode_outcome(OpId op, const ErasedOutcome& outcome) = 0;
    // Drops EventKind values this build does not know; an error only drops that one Subscribe.
    virtual Result<EventFilter> decode_filter(std::span<const u8> filter) = 0;
    // nullopt for an event the API does not carry.
    [[nodiscard]] virtual std::optional<contracts::ipc::WireEvent> encode_event(const EventEnvelope& event) = 0;

    // `target` is an encoded reboot.api.v1 SecretTarget.
    virtual Result<void> put_secret(const ConnectionInfo& from, std::span<const u8> target, SecretBytes secret) = 0;
    // Only the host join password; anything else is ipc.reveal_refused.
    virtual Result<SecretBytes> reveal_secret(const ConnectionInfo& from, std::span<const u8> target) = 0;
};

}  // namespace rb::ipc
