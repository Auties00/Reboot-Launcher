#include "member_reader.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include "messages.hpp"
#include "reboot/storage/json_values.hpp"

namespace reboot::storage {

namespace json = boost::json;

Diagnostic wrong_type(std::string_view expected) {
    return invalid_input(msg::kWrongType).arg("expected", expected).build();
}

Diagnostic missing_member(std::string_view member) {
    return invalid_input(msg::kMissingMember).arg("member", member).build();
}

Result<const json::object*> object_from_json(const json::value& value) {
    if (const json::object* object = value.if_object()) return object;
    return std::unexpected(wrong_type("object"));
}

Result<u32> u32_from_json(const json::value& value) {
    Result<u64> number = u64_from_json(value);
    if (!number) return std::unexpected(std::move(number.error()));
    if (*number > std::numeric_limits<u32>::max())
        return invalid_input(msg::kOutOfRange).arg("value", *number).fail();
    return static_cast<u32>(*number);
}

const json::value* MemberReader::find(std::string_view name) {
    known_.emplace_back(name);
    return values_.if_contains(json::string_view(name.data(), name.size()));
}

json::object MemberReader::unknown() const {
    json::object out;
    for (const json::key_value_pair& member : values_) {
        const std::string_view key(member.key().data(), member.key().size());
        if (std::ranges::find(known_, key) == known_.end()) out.emplace(member.key(), member.value());
    }
    return out;
}

void append_unknown(json::object& out, const json::object& unknown) {
    for (const json::key_value_pair& member : unknown)
        if (!out.contains(member.key())) out.emplace(member.key(), member.value());
}

}  // namespace reboot::storage
