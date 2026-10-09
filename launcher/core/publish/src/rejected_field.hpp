#pragma once

#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::publish {

// The field a BAD_REQUEST names ("invalid <field>"). The edge's text is untrusted, so it is only
// matched against this closed set and never shown.
enum class RejectedField : u8 { Unknown, Id, Name, Description, Version, Author, GamePort, Password, MaxPlayers, Players };

[[nodiscard]] RejectedField rejected_field(std::string_view edge_message) noexcept;
// The rbsb/1 field name, or "unknown".
[[nodiscard]] std::string_view field_id(RejectedField field) noexcept;
// publish.edge_rejected with the field id as its argument.
[[nodiscard]] Diagnostic edge_rejected(std::string_view edge_message);

}  // namespace rb::publish
