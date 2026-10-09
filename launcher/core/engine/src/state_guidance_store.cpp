#include "state_guidance_store.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>

#include "reboot/contracts/common.hpp"
#include "reboot/storage/json_values.hpp"
#include "reboot/ux/notice.hpp"
#include "reboot/ux/onboarding_step.hpp"
#include "state_extras.hpp"

namespace rb::engine {

namespace json = boost::json;

namespace {

constexpr std::array<std::string_view, 4> kStatusNames{"offered", "in_progress", "completed", "exited"};
constexpr std::array<std::string_view, 4> kStepStateNames{"pending", "current", "done", "skipped"};

template <class E, std::size_t N>
[[nodiscard]] std::optional<E> enum_named(const json::value* value, const std::array<std::string_view, N>& names) {
    if (value == nullptr || !value->is_string()) return std::nullopt;
    const std::string_view text = value->get_string();
    for (std::size_t i = 0; i < N; ++i)
        if (names[i] == text) return static_cast<E>(i);
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string_view> text_of(const json::object& object, std::string_view key) {
    const json::value* value = object.if_contains(key);
    if (value == nullptr || !value->is_string()) return std::nullopt;
    return std::string_view(value->get_string());
}

[[nodiscard]] json::value write_args(const std::vector<std::pair<std::string, Arg>>& args) {
    json::array out;
    for (const auto& [name, value] : args) {
        json::object arg;
        arg["name"] = name;
        arg["kind"] = static_cast<std::uint64_t>(value.index());
        arg["value"] = contracts::common::detail::arg_text(value);
        out.push_back(std::move(arg));
    }
    return out;
}

[[nodiscard]] std::vector<std::pair<std::string, Arg>> read_args(const json::value* value) {
    std::vector<std::pair<std::string, Arg>> args;
    if (value == nullptr || !value->is_array()) return args;
    for (const json::value& item : value->get_array()) {
        if (!item.is_object()) continue;
        const json::object& object = item.get_object();
        const auto name = text_of(object, "name");
        const auto text = text_of(object, "value");
        const json::value* kind = object.if_contains("kind");
        if (!name || !text || kind == nullptr || !kind->is_number()) continue;
        const auto index = kind->to_number<std::uint64_t>();
        if (index > static_cast<std::uint64_t>(contracts::common::ArgKind::SemVer)) continue;
        contracts::common::WireArg wire{std::string(*name), static_cast<contracts::common::ArgKind>(index),
                                        std::string(*text)};
        args.emplace_back(wire.name, contracts::common::detail::arg_value(wire));
    }
    return args;
}

[[nodiscard]] json::value write_state(const ux::GuidanceState& state) {
    json::object onboarding;
    onboarding["status"] = kStatusNames[static_cast<std::size_t>(state.onboarding.status)];
    if (state.onboarding.current) onboarding["current"] = ux::persisted_name(*state.onboarding.current);
    json::array steps;
    for (const ux::StepRecord& step : state.onboarding.steps) {
        json::object record;
        record["step"] = ux::persisted_name(step.step);
        record["state"] = kStepStateNames[static_cast<std::size_t>(step.state)];
        if (step.chosen) record["chosen"] = ux::persisted_name(*step.chosen);
        steps.push_back(std::move(record));
    }
    onboarding["steps"] = std::move(steps);

    json::array notices;
    for (const ux::OneTimeNoticeRecord& notice : state.notices) {
        json::object record;
        record["kind"] = ux::persisted_name(notice.key.kind);
        if (notice.key.session) record["session"] = storage::id_to_json(*notice.key.session);
        record["created_at"] = storage::time_to_json(notice.created_at);
        record["args"] = write_args(notice.args);
        record["dismissed"] = notice.dismissed;
        notices.push_back(std::move(record));
    }

    json::object out;
    out["onboarding"] = std::move(onboarding);
    out["notices"] = std::move(notices);
    return out;
}

[[nodiscard]] ux::GuidanceState read_state(const json::value* value) {
    ux::GuidanceState state;
    if (value == nullptr || !value->is_object()) return state;
    const json::object& root = value->get_object();

    if (const json::value* onboarding = root.if_contains("onboarding"); onboarding && onboarding->is_object()) {
        const json::object& record = onboarding->get_object();
        if (auto status = enum_named<ux::OnboardingStatus>(record.if_contains("status"), kStatusNames))
            state.onboarding.status = *status;
        if (auto current = text_of(record, "current")) state.onboarding.current = ux::parse_step_id(*current);
        if (const json::value* steps = record.if_contains("steps"); steps && steps->is_array()) {
            for (const json::value& item : steps->get_array()) {
                if (!item.is_object()) continue;
                const json::object& step = item.get_object();
                const auto name = text_of(step, "step");
                const std::optional<ux::StepId> id = name ? ux::parse_step_id(*name) : std::nullopt;
                if (!id) continue;
                ux::StepRecord parsed{.step = *id};
                if (auto step_state = enum_named<ux::StepState>(step.if_contains("state"), kStepStateNames))
                    parsed.state = *step_state;
                if (auto chosen = text_of(step, "chosen")) parsed.chosen = ux::parse_choice_id(*chosen);
                state.onboarding.steps.push_back(std::move(parsed));
            }
        }
    }

    if (const json::value* notices = root.if_contains("notices"); notices && notices->is_array()) {
        for (const json::value& item : notices->get_array()) {
            if (!item.is_object()) continue;
            const json::object& record = item.get_object();
            const auto kind_name = text_of(record, "kind");
            const std::optional<ux::NoticeKind> kind = kind_name ? ux::parse_notice_kind(*kind_name) : std::nullopt;
            if (!kind) continue;
            ux::OneTimeNoticeRecord notice;
            notice.key.kind = *kind;
            if (const json::value* session = record.if_contains("session"))
                if (Result<SessionId> id = storage::id_from_json<SessionTag>(*session)) notice.key.session = *id;
            if (const json::value* created = record.if_contains("created_at"))
                if (auto at = storage::time_from_json(*created)) notice.created_at = *at;
            notice.args = read_args(record.if_contains("args"));
            if (const json::value* dismissed = record.if_contains("dismissed"); dismissed && dismissed->is_bool())
                notice.dismissed = dismissed->get_bool();
            state.notices.push_back(std::move(notice));
        }
    }
    return state;
}

}  // namespace

StateGuidanceStore::StateGuidanceStore(storage::DocumentStore<storage::StateDocument>& state)
    : state_(state), current_(read_state(state_extra(state.get(), kGuidanceKey))) {}

Result<void> StateGuidanceStore::replace(ux::GuidanceState state) {
    std::vector<std::string> dismissed;
    for (const ux::OneTimeNoticeRecord& notice : state.notices)
        if (notice.dismissed) dismissed.emplace_back(ux::persisted_name(notice.key.kind));
    Result<u64> written = state_.update([value = write_state(state), dismissed = std::move(dismissed)](
                                            storage::StateDocument& document) mutable {
        document.unknown[kGuidanceKey] = std::move(value);
        document.dismissed_notices = std::move(dismissed);
    });
    if (!written) return std::unexpected(std::move(written.error()));
    current_ = std::move(state);
    return {};
}

}  // namespace rb::engine
