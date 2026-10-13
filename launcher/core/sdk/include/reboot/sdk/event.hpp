#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <variant>

#include "reboot/api/codec.hpp"
#include "reboot/api/v1/common.hpp"
#include "reboot/api/v1/events.hpp"
#include "reboot/base/types.hpp"
#include "reboot/sdk/event_data.hpp"

namespace rb::sdk {

struct Event {
    api::EventKind kind{};
    std::uint64_t epoch = 0;
    // Global within an epoch.
    std::uint64_t seq = 0;
    std::optional<Uuid> session;
    std::optional<std::uint64_t> op_id;
    EventData data;

    template <class T>
    [[nodiscard]] const T* get() const noexcept {
        return std::get_if<T>(&data);
    }
};

// nullopt for an event this build cannot read, a kind it does not know or a payload that does not
// decode; subscriptions skip those.
[[nodiscard]] inline std::optional<Event> decode_event(std::span<const std::uint8_t> bytes) {
    std::expected<api::Event, api::DecodeError> envelope = api::decode<api::Event>(bytes);
    if (!envelope) return std::nullopt;
    Event event{envelope->kind, envelope->epoch, envelope->seq, envelope->session, envelope->op_id, Closed{}};
    if (std::optional<EventData> marker = marker_data(envelope->kind)) {
        event.data = std::move(*marker);
        return event;
    }
    if (!api::has_payload(envelope->kind)) return std::nullopt;
    std::expected<api::EventPayload, api::DecodeError> payload = api::decode<api::EventPayload>(envelope->payload);
    if (!payload || api::event_kind(*payload) != envelope->kind) return std::nullopt;
    std::optional<EventData> data = take_data(std::move(*payload));
    if (!data) return std::nullopt;
    event.data = std::move(*data);
    return event;
}

}  // namespace rb::sdk
