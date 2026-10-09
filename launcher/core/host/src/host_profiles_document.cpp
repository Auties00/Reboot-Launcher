#include "reboot/host/host_profiles_document.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include <boost/json/array.hpp>
#include <boost/json/value.hpp>

#include "messages.hpp"
#include "reboot/host/host_error.hpp"
#include "reboot/storage/json_values.hpp"

namespace reboot::host {

namespace {

namespace json = boost::json;

constexpr std::array<std::string_view, 3> kMatchEndActionNames{"restart", "shutdown", "none"};
constexpr std::string_view kManualStart = "manual";

[[nodiscard]] std::unexpected<Diagnostic> invalid(std::string_view member) {
    return make_diag(ErrorDomain::Host, msg::kProfileMemberInvalid)
        .arg("member", member)
        .kind(ErrorKind::InvalidInput)
        .fail();
}

// JSON null reads as absent.
[[nodiscard]] const json::value* member(const json::object& object, std::string_view name) {
    const json::value* value = object.if_contains(json::string_view(name.data(), name.size()));
    return value != nullptr && !value->is_null() ? value : nullptr;
}

[[nodiscard]] Result<const json::object*> as_object(const json::value& value, std::string_view name) {
    if (!value.is_object()) return invalid(name);
    return &value.get_object();
}

[[nodiscard]] Result<const json::array*> as_array(const json::value& value, std::string_view name) {
    if (!value.is_array()) return invalid(name);
    return &value.get_array();
}

[[nodiscard]] Result<u32> as_u32(const json::value& value, std::string_view name) {
    Result<u64> number = storage::u64_from_json(value);
    if (!number) return std::unexpected(std::move(number.error()));
    if (*number > std::numeric_limits<u32>::max()) return invalid(name);
    return static_cast<u32>(*number);
}

template <class E, std::size_t N>
[[nodiscard]] Result<E> as_enum(const json::value& value, const std::array<std::string_view, N>& names) {
    return storage::name_index_from_json(value, names).transform([](std::size_t index) {
        return static_cast<E>(index);
    });
}

[[nodiscard]] Result<HostVersion> version_from_json(const json::value& value) {
    Result<const json::object*> object = as_object(value, "version");
    if (!object) return std::unexpected(std::move(object.error()));
    const json::value* text = member(**object, "version");
    const json::value* cl = member(**object, "cl");
    if (text == nullptr || cl == nullptr) return invalid("version");
    Result<GameVersion> version = storage::game_version_from_json(*text);
    if (!version) return std::unexpected(std::move(version.error()));
    Result<u32> changelist = as_u32(*cl, "version.cl");
    if (!changelist) return std::unexpected(std::move(changelist.error()));
    return HostVersion{*version, Changelist{*changelist}};
}

[[nodiscard]] Result<PortPolicy> port_from_json(const json::value& value) {
    Result<const json::object*> object = as_object(value, "port");
    if (!object) return std::unexpected(std::move(object.error()));
    if (const json::value* pinned = member(**object, "pinned")) {
        Result<Port> port = storage::port_from_json(*pinned);
        if (!port) return std::unexpected(std::move(port.error()));
        return PinnedPorts{*port};
    }
    const json::value* automatic = member(**object, "auto");
    if (automatic == nullptr) return invalid("port");
    Result<const json::object*> range = as_object(*automatic, "port.auto");
    if (!range) return std::unexpected(std::move(range.error()));
    const json::value* first = member(**range, "first");
    const json::value* last = member(**range, "last");
    if (first == nullptr || last == nullptr) return invalid("port.auto");
    Result<Port> first_port = storage::port_from_json(*first);
    if (!first_port) return std::unexpected(std::move(first_port.error()));
    Result<Port> last_port = storage::port_from_json(*last);
    if (!last_port) return std::unexpected(std::move(last_port.error()));
    return AutoPorts{PortRange{*first_port, *last_port}};
}

[[nodiscard]] Result<gameserver::MatchSettings> match_from_json(const json::value& value) {
    Result<const json::object*> object = as_object(value, "match");
    if (!object) return std::unexpected(std::move(object.error()));
    gameserver::MatchSettings match;
    if (const json::value* playlist = member(**object, "playlist")) {
        Result<std::string> text = storage::string_from_json(*playlist);
        if (!text) return std::unexpected(std::move(text.error()));
        match.playlist = std::move(*text);
    }
    if (const json::value* start = member(**object, "start")) {
        if (start->is_string()) {
            const json::string& text = start->get_string();
            if (std::string_view(text.data(), text.size()) != kManualStart) return invalid("match.start");
            match.start = gameserver::ManualStart{};
        } else {
            Result<const json::object*> automatic = as_object(*start, "match.start");
            if (!automatic) return std::unexpected(std::move(automatic.error()));
            const json::value* players = member(**automatic, "auto_at_players");
            if (players == nullptr) return invalid("match.start");
            Result<u32> count = as_u32(*players, "match.start");
            if (!count) return std::unexpected(std::move(count.error()));
            match.start = gameserver::AutoAtPlayers{*count};
        }
    }
    if (const json::value* max_players = member(**object, "max_players")) {
        Result<u32> count = as_u32(*max_players, "match.max_players");
        if (!count) return std::unexpected(std::move(count.error()));
        match.max_players = *count;
    }
    if (const json::value* tick_rate = member(**object, "tick_rate")) {
        Result<u32> rate = as_u32(*tick_rate, "match.tick_rate");
        if (!rate) return std::unexpected(std::move(rate.error()));
        match.tick_rate = *rate;
    }
    return match;
}

[[nodiscard]] Result<MatchEndPolicy> match_end_from_json(const json::value& value) {
    Result<const json::object*> object = as_object(value, "match_end");
    if (!object) return std::unexpected(std::move(object.error()));
    MatchEndPolicy policy;
    if (const json::value* action = member(**object, "action")) {
        Result<MatchEndAction> parsed = as_enum<MatchEndAction>(*action, kMatchEndActionNames);
        if (!parsed) return std::unexpected(std::move(parsed.error()));
        policy.action = *parsed;
    }
    if (const json::value* delay = member(**object, "delay_s")) {
        Result<u32> seconds = as_u32(*delay, "match_end.delay_s");
        if (!seconds) return std::unexpected(std::move(seconds.error()));
        policy.delay = std::chrono::seconds{*seconds};
    }
    return policy;
}

[[nodiscard]] Result<HostBan> ban_from_json(const json::value& value) {
    Result<const json::object*> object = as_object(value, "operators.bans");
    if (!object) return std::unexpected(std::move(object.error()));
    HostBan ban;
    if (const json::value* address = member(**object, "address")) {
        Result<std::string> text = storage::string_from_json(*address);
        if (!text) return std::unexpected(std::move(text.error()));
        Result<IpCidr> cidr = IpCidr::parse(*text);
        if (!cidr) return std::unexpected(std::move(cidr.error()));
        ban.address = *cidr;
    }
    if (const json::value* account = member(**object, "account_id")) {
        Result<std::string> text = storage::string_from_json(*account);
        if (!text) return std::unexpected(std::move(text.error()));
        ban.account_id = std::move(*text);
    }
    if (const json::value* reason = member(**object, "reason")) {
        Result<std::string> text = storage::string_from_json(*reason);
        if (!text) return std::unexpected(std::move(text.error()));
        ban.reason = std::move(*text);
    }
    if (const json::value* created = member(**object, "created")) {
        Result<std::chrono::system_clock::time_point> time = storage::time_from_json(*created);
        if (!time) return std::unexpected(std::move(time.error()));
        ban.created = *time;
    }
    if (const json::value* expires = member(**object, "expires")) {
        Result<std::chrono::system_clock::time_point> time = storage::time_from_json(*expires);
        if (!time) return std::unexpected(std::move(time.error()));
        ban.expires = *time;
    }
    return ban;
}

[[nodiscard]] Result<OperatorPolicy> operators_from_json(const json::value& value) {
    Result<const json::object*> object = as_object(value, "operators");
    if (!object) return std::unexpected(std::move(object.error()));
    OperatorPolicy policy;
    if (const json::value* cidrs = member(**object, "cidrs")) {
        Result<const json::array*> array = as_array(*cidrs, "operators.cidrs");
        if (!array) return std::unexpected(std::move(array.error()));
        for (const json::value& entry : **array) {
            Result<std::string> text = storage::string_from_json(entry);
            if (!text) return std::unexpected(std::move(text.error()));
            Result<IpCidr> cidr = IpCidr::parse(*text);
            if (!cidr) return std::unexpected(std::move(cidr.error()));
            policy.operator_cidrs.push_back(*cidr);
        }
    }
    if (const json::value* bans = member(**object, "bans")) {
        Result<const json::array*> array = as_array(*bans, "operators.bans");
        if (!array) return std::unexpected(std::move(array.error()));
        for (const json::value& entry : **array) {
            Result<HostBan> ban = ban_from_json(entry);
            if (!ban) return std::unexpected(std::move(ban.error()));
            policy.bans.push_back(std::move(*ban));
        }
    }
    return policy;
}

// What a built-in profile falls back to, member by member.
[[nodiscard]] HostProfile builtin_default(const HostProfileId& id) {
    return id == kAutoProfileId ? auto_profile() : new_profile(kDefaultProfileId, "default", HostListing::Unlisted);
}

// Reads every member present into `profile`. A strict read fails on the first bad member; a
// lenient one (a built-in) keeps the member's current value and reports it.
class ProfileReader {
public:
    ProfileReader(const json::object& object, std::string path, bool lenient, std::vector<storage::ValueIssue>& issues)
        : object_(object), path_(std::move(path)), lenient_(lenient), issues_(issues) {}

    template <class T, class Parse>
    Result<void> read(std::string_view name, T& target, Parse&& parse) {
        const json::value* value = member(object_, name);
        if (value == nullptr) return {};
        Result<T> parsed = parse(*value);
        if (parsed) {
            target = std::move(*parsed);
            return {};
        }
        if (!lenient_) return std::unexpected(std::move(parsed.error()));
        issues_.push_back({path_ + "." + std::string(name), std::move(parsed.error())});
        return {};
    }

private:
    const json::object& object_;
    std::string path_;
    bool lenient_;
    std::vector<storage::ValueIssue>& issues_;
};

[[nodiscard]] Result<HostProfile> members_from_json(const json::object& object, HostProfile profile,
                                                    const std::string& path, bool lenient,
                                                    std::vector<storage::ValueIssue>& issues) {
    ProfileReader reader(object, path, lenient, issues);
    const auto string = [](const json::value& value) { return storage::string_from_json(value); };
    const auto boolean = [](const json::value& value) { return storage::bool_from_json(value); };
    std::optional<BuildId> build = profile.build;
    std::optional<HostVersion> version = profile.version;
    std::optional<HostUpdatePolicy> update_policy = profile.update_policy;

    Result<void> step = reader.read("revision", profile.revision, [](const json::value& value) {
        return storage::u64_from_json(value);
    });
    if (step) step = reader.read("name", profile.name, string);
    if (step) step = reader.read("build", build, [](const json::value& value) -> Result<std::optional<BuildId>> {
        return storage::id_from_json<BuildTag>(value);
    });
    if (step)
        step = reader.read("version", version, [](const json::value& value) -> Result<std::optional<HostVersion>> {
            return version_from_json(value);
        });
    if (step) step = reader.read("port", profile.port, port_from_json);
    if (step) step = reader.read("port_mapping", profile.port_mapping, boolean);
    if (step) step = reader.read("listing", profile.listing, [](const json::value& value) {
        return storage::enum_from_json<HostListing>(value);
    });
    if (step) step = reader.read("server_name", profile.server_name, string);
    if (step) step = reader.read("description", profile.description, string);
    if (step) step = reader.read("match", profile.match, match_from_json);
    if (step) step = reader.read("match_end", profile.match_end, match_end_from_json);
    if (step) step = reader.read("operators", profile.operators, operators_from_json);
    if (step)
        step = reader.read("update_policy", update_policy,
                           [](const json::value& value) -> Result<std::optional<HostUpdatePolicy>> {
                               return storage::enum_from_json<HostUpdatePolicy>(value);
                           });
    if (!step) return std::unexpected(std::move(step.error()));
    profile.build = build;
    profile.version = version;
    profile.update_policy = update_policy;
    return profile;
}

// Puts each member validate() refuses back to the built-in default.
[[nodiscard]] HostProfile repair_builtin(HostProfile profile, const std::string& path,
                                         std::vector<storage::ValueIssue>& issues) {
    const HostProfile fallback = builtin_default(profile.id);
    const auto reset = [&](std::string_view name, Diagnostic reason, auto& target, const auto& value) {
        issues.push_back({path + "." + std::string(name), std::move(reason)});
        target = value;
    };
    const auto refused = [](HostErrorCode code) { return to_diagnostic(HostError{.code = code}); };
    if (profile.name.empty() || profile.name.size() > kMaxProfileNameLength)
        reset("name", refused(profile.name.empty() ? HostErrorCode::ProfileNameEmpty : HostErrorCode::ProfileNameTooLong),
              profile.name, fallback.name);
    if (profile.build && profile.version) {
        reset("build", refused(HostErrorCode::BuildAndVersion), profile.build, fallback.build);
        profile.version = fallback.version;
    }
    HostProfile probe = fallback;
    probe.server_name = profile.server_name;
    if (Result<HostProfile> checked = validate(probe); !checked)
        reset("server_name", std::move(checked.error()), profile.server_name, fallback.server_name);
    probe = fallback;
    probe.description = profile.description;
    if (Result<HostProfile> checked = validate(probe); !checked)
        reset("description", std::move(checked.error()), profile.description, fallback.description);
    if (profile.is_auto() && profile.listing == HostListing::Listed)
        reset("listing", refused(HostErrorCode::AutoProfileListed), profile.listing, fallback.listing);
    if (Result<void> port = validate(profile.port); !port) reset("port", std::move(port.error()), profile.port, fallback.port);
    if (Result<void> match_end = validate(profile.match_end); !match_end)
        reset("match_end", std::move(match_end.error()), profile.match_end, fallback.match_end);
    if (Result<OperatorPolicy> operators = normalize(profile.operators); !operators)
        reset("operators", std::move(operators.error()), profile.operators, fallback.operators);
    return profile;
}

[[nodiscard]] bool same_name(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

[[nodiscard]] Result<HostProfile> profile_from_json(const json::value& value, const std::string& path,
                                                    std::vector<storage::ValueIssue>& issues) {
    Result<const json::object*> object = as_object(value, "profiles");
    if (!object) return std::unexpected(std::move(object.error()));
    const json::value* id_json = member(**object, "id");
    if (id_json == nullptr) return invalid("id");
    Result<HostProfileId> id = storage::id_from_json<HostProfileTag>(*id_json);
    if (!id) return std::unexpected(std::move(id.error()));

    const bool builtin = *id == kAutoProfileId || *id == kDefaultProfileId;
    if (!builtin) {
        if (member(**object, "name") == nullptr) return invalid("name");
        HostProfile base;
        base.id = *id;
        Result<HostProfile> profile = members_from_json(**object, std::move(base), path, false, issues);
        if (!profile) return profile;
        return validate(std::move(*profile));
    }
    Result<HostProfile> profile = members_from_json(**object, builtin_default(*id), path, true, issues);
    if (!profile) return profile;
    if (Result<HostProfile> valid = validate(*profile)) return valid;
    if (Result<HostProfile> repaired = validate(repair_builtin(std::move(*profile), path, issues))) return repaired;
    return builtin_default(*id);
}

[[nodiscard]] json::value port_to_json(const PortPolicy& policy) {
    json::object out;
    if (const auto* pinned = std::get_if<PinnedPorts>(&policy)) {
        out.emplace("pinned", storage::port_to_json(pinned->first));
    } else {
        const PortRange& range = std::get<AutoPorts>(policy).range;
        json::object range_json;
        range_json.emplace("first", storage::port_to_json(range.first));
        range_json.emplace("last", storage::port_to_json(range.last));
        out.emplace("auto", std::move(range_json));
    }
    return out;
}

[[nodiscard]] json::value match_to_json(const gameserver::MatchSettings& match) {
    json::object out;
    out.emplace("playlist", match.playlist);
    if (const auto* automatic = std::get_if<gameserver::AutoAtPlayers>(&match.start)) {
        json::object start;
        start.emplace("auto_at_players", automatic->players);
        out.emplace("start", std::move(start));
    } else {
        out.emplace("start", storage::name_to_json(kManualStart));
    }
    out.emplace("max_players", match.max_players);
    out.emplace("tick_rate", match.tick_rate);
    return out;
}

[[nodiscard]] json::value operators_to_json(const OperatorPolicy& policy) {
    json::array cidrs;
    for (const IpCidr& cidr : policy.operator_cidrs) cidrs.emplace_back(cidr.to_string());
    json::array bans;
    for (const HostBan& ban : policy.bans) {
        json::object entry;
        if (ban.address) entry.emplace("address", ban.address->to_string());
        if (ban.account_id) entry.emplace("account_id", *ban.account_id);
        entry.emplace("reason", ban.reason);
        entry.emplace("created", storage::time_to_json(ban.created));
        if (ban.expires) entry.emplace("expires", storage::time_to_json(*ban.expires));
        bans.emplace_back(std::move(entry));
    }
    json::object out;
    out.emplace("cidrs", std::move(cidrs));
    out.emplace("bans", std::move(bans));
    return out;
}

[[nodiscard]] json::object profile_to_json(const HostProfile& profile) {
    json::object out;
    out.emplace("id", storage::id_to_json(profile.id));
    out.emplace("revision", profile.revision);
    out.emplace("name", profile.name);
    if (profile.build) out.emplace("build", storage::id_to_json(*profile.build));
    if (profile.version) {
        json::object version;
        version.emplace("version", storage::game_version_to_json(profile.version->version));
        version.emplace("cl", profile.version->cl.value);
        out.emplace("version", std::move(version));
    }
    out.emplace("port", port_to_json(profile.port));
    out.emplace("port_mapping", profile.port_mapping);
    out.emplace("listing", storage::enum_to_json(profile.listing));
    out.emplace("server_name", profile.server_name);
    out.emplace("description", profile.description);
    out.emplace("match", match_to_json(profile.match));
    json::object match_end;
    match_end.emplace("action", storage::name_to_json(kMatchEndActionNames[static_cast<std::size_t>(profile.match_end.action)]));
    match_end.emplace("delay_s", profile.match_end.delay.count());
    out.emplace("match_end", std::move(match_end));
    out.emplace("operators", operators_to_json(profile.operators));
    if (profile.update_policy) out.emplace("update_policy", storage::enum_to_json(*profile.update_policy));
    return out;
}

}  // namespace

HostProfilesDocument HostProfilesDocument::read(const json::object& values, std::vector<storage::ValueIssue>& issues) {
    HostProfilesDocument document;
    for (const json::key_value_pair& entry : values)
        if (entry.key() != "profiles") document.unknown.emplace(entry.key(), entry.value());

    const json::value* raw = member(values, "profiles");
    if (raw == nullptr) return document;
    if (!raw->is_array()) {
        issues.push_back({"profiles", invalid("profiles").error()});
        return document;
    }
    const json::array& array = raw->get_array();
    for (std::size_t i = 0; i < array.size(); ++i) {
        const std::string path = "profiles[" + std::to_string(i) + "]";
        Result<HostProfile> profile = profile_from_json(array[i], path, issues);
        if (!profile) {
            issues.push_back({path, std::move(profile.error())});
            continue;
        }
        bool duplicate = false;
        for (const HostProfile& kept : document.profiles) {
            if (kept.id == profile->id) {
                issues.push_back({path, make_diag(ErrorDomain::Host, msg::kDuplicateProfile)
                                            .arg("profile", format_uuid(profile->id.value))
                                            .kind(ErrorKind::Conflict)
                                            .build()});
                duplicate = true;
                break;
            }
            if (same_name(kept.name, profile->name)) {
                issues.push_back({path, to_diagnostic(HostError{.code = HostErrorCode::ProfileNameTaken,
                                                                .name = profile->name})});
                duplicate = true;
                break;
            }
        }
        if (!duplicate) document.profiles.push_back(std::move(*profile));
    }
    return document;
}

json::object HostProfilesDocument::write() const {
    json::array profiles_json;
    for (const HostProfile& profile : profiles) profiles_json.emplace_back(profile_to_json(profile));
    json::object out;
    out.emplace("profiles", std::move(profiles_json));
    for (const json::key_value_pair& entry : unknown)
        if (!out.contains(entry.key())) out.emplace(entry.key(), entry.value());
    return out;
}

Result<json::object> HostProfilesDocument::upgrade(json::object values, u32) { return values; }

}  // namespace reboot::host
