#include "reboot/updates/pending_update_marker.hpp"

#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>
#include <boost/system/error_code.hpp>

#include "messages.hpp"
#include "reboot/storage/json_values.hpp"

namespace rb::updates {

namespace json = boost::json;

namespace {

[[nodiscard]] std::unexpected<Diagnostic> malformed(std::string_view field) {
    return make_diag(ErrorDomain::Updates, msg::kMarkerMalformed).arg("field", field).fail();
}

[[nodiscard]] std::unexpected<Diagnostic> malformed(std::string_view field, Diagnostic cause) {
    return make_diag(ErrorDomain::Updates, msg::kMarkerMalformed).arg("field", field).cause(std::move(cause)).fail();
}

[[nodiscard]] Result<SemVer> version_field(const json::object& object, std::string_view name) {
    const json::value* raw = object.if_contains(name);
    if (raw == nullptr) return malformed(name);
    Result<SemVer> parsed = storage::semver_from_json(*raw);
    if (!parsed) return malformed(name, std::move(parsed.error()));
    return parsed;
}

}  // namespace

std::vector<u8> encode_marker(const PendingUpdateMarker& marker) {
    json::object out;
    out.emplace("from", storage::semver_to_json(marker.from));
    out.emplace("to", storage::semver_to_json(marker.to));
    out.emplace("attempts", marker.attempts);
    out.emplace("started_at", storage::time_to_json(marker.started_at));
    const std::string text = json::serialize(out);
    return {text.begin(), text.end()};
}

Result<PendingUpdateMarker> parse_marker(std::span<const u8> bytes) {
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    boost::system::error_code error;
    const json::value root = json::parse(text, error);
    if (error || !root.is_object()) return malformed("marker");
    const json::object& object = root.get_object();

    PendingUpdateMarker marker;
    Result<SemVer> from = version_field(object, "from");
    if (!from) return std::unexpected(std::move(from.error()));
    marker.from = std::move(*from);
    Result<SemVer> to = version_field(object, "to");
    if (!to) return std::unexpected(std::move(to.error()));
    marker.to = std::move(*to);

    const json::value* attempts = object.if_contains("attempts");
    if (attempts == nullptr) return malformed("attempts");
    Result<u64> count = storage::u64_from_json(*attempts);
    if (!count) return malformed("attempts", std::move(count.error()));
    if (*count > std::numeric_limits<u32>::max()) return malformed("attempts");
    marker.attempts = static_cast<u32>(*count);

    const json::value* started_at = object.if_contains("started_at");
    if (started_at == nullptr) return malformed("started_at");
    Result<std::chrono::system_clock::time_point> time = storage::time_from_json(*started_at);
    if (!time) return malformed("started_at", std::move(time.error()));
    marker.started_at = *time;
    return marker;
}

MarkerVerdict judge_marker(const PendingUpdateMarker& marker, const SemVer& running) {
    if (running == marker.to)
        return marker.attempts <= kMaxUpdateAttempts ? MarkerVerdict::SelfTest : MarkerVerdict::GiveUp;
    if (running == marker.from) return MarkerVerdict::NotApplied;
    return MarkerVerdict::Foreign;
}

}  // namespace rb::updates
