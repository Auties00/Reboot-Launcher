#include "reboot/ux/onboarding_step.hpp"

#include <array>
#include <utility>

namespace reboot::ux {

namespace {

constexpr std::array<std::pair<StepId, std::string_view>, 9> kStepNames{{
    {StepId::Welcome, "welcome"},
    {StepId::Prerequisites, "prerequisites"},
    {StepId::Profile, "profile"},
    {StepId::Library, "library"},
    {StepId::Play, "play"},
    {StepId::Browser, "browser"},
    {StepId::HostListing, "host_listing"},
    {StepId::Backend, "backend"},
    {StepId::Finish, "finish"},
}};

constexpr std::array<std::pair<OnboardingChoiceId, std::string_view>, 2> kChoiceNames{{
    {OnboardingChoiceId::ListPublicly, "list_publicly"},
    {OnboardingChoiceId::KeepUnlisted, "keep_unlisted"},
}};

template <class E, std::size_t N>
[[nodiscard]] std::string_view name_of(const std::array<std::pair<E, std::string_view>, N>& names, E value) {
    for (const auto& [id, name] : names)
        if (id == value) return name;
    return {};
}

template <class E, std::size_t N>
[[nodiscard]] std::optional<E> parse_name(const std::array<std::pair<E, std::string_view>, N>& names,
                                          std::string_view text) {
    for (const auto& [id, name] : names)
        if (name == text) return id;
    return std::nullopt;
}

}  // namespace

std::string_view persisted_name(StepId step) { return name_of(kStepNames, step); }
std::optional<StepId> parse_step_id(std::string_view name) { return parse_name(kStepNames, name); }
std::string_view persisted_name(OnboardingChoiceId choice) { return name_of(kChoiceNames, choice); }
std::optional<OnboardingChoiceId> parse_choice_id(std::string_view name) { return parse_name(kChoiceNames, name); }

}  // namespace reboot::ux
