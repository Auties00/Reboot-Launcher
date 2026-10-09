#include "rejected_field.hpp"

#include <array>
#include <utility>

#include "messages.hpp"

namespace rb::publish {

namespace {

constexpr std::string_view kInvalidPrefix = "invalid ";

constexpr std::array<std::pair<std::string_view, RejectedField>, 9> kFields{{
    {"id", RejectedField::Id},
    {"name", RejectedField::Name},
    {"description", RejectedField::Description},
    {"version", RejectedField::Version},
    {"author", RejectedField::Author},
    {"game_port", RejectedField::GamePort},
    {"password", RejectedField::Password},
    {"max_players", RejectedField::MaxPlayers},
    {"players", RejectedField::Players},
}};

}  // namespace

RejectedField rejected_field(std::string_view edge_message) noexcept {
    if (!edge_message.starts_with(kInvalidPrefix)) return RejectedField::Unknown;
    const std::string_view name = edge_message.substr(kInvalidPrefix.size());
    for (const auto& [wire_name, field] : kFields)
        if (wire_name == name) return field;
    return RejectedField::Unknown;
}

std::string_view field_id(RejectedField field) noexcept {
    for (const auto& [wire_name, known] : kFields)
        if (known == field) return wire_name;
    return "unknown";
}

Diagnostic edge_rejected(std::string_view edge_message) {
    return make_diag(ErrorDomain::Publish, msg::kEdgeRejected)
        .kind(ErrorKind::InvalidInput)
        .arg("field", field_id(rejected_field(edge_message)))
        .build();
}

}  // namespace rb::publish
