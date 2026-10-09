#include "reboot/foundation/types.hpp"

#include "messages.hpp"
#include "reboot/foundation/diag.hpp"

namespace rb {

Result<Uuid> parse_uuid(std::string_view text) {
    const auto invalid = [&] {
        return make_diag(ErrorDomain::Foundation, msg::kInvalidUuid).kind(ErrorKind::InvalidInput).arg("text", text).fail();
    };
    if (text.size() != 36) return invalid();
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const bool dash_position = i == 8 || i == 13 || i == 18 || i == 23;
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (dash_position ? c != '-' : !hex) return invalid();
    }
    const std::optional<Uuid> uuid = Uuid::parse(text);
    if (!uuid) return invalid();
    return *uuid;
}

}  // namespace rb
