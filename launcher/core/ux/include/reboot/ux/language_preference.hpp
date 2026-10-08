#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "reboot/foundation/diag.hpp"
#include "reboot/ux/language_tag.hpp"

namespace reboot::ux {

// Capabilities: localization.strings-and-language, localization.+49.
// ui.language: "system" (the default) or a BCP 47 tag.
class LanguagePreference {
public:
    static constexpr std::string_view kSystemValue = "system";

    LanguagePreference() = default;
    explicit LanguagePreference(LanguageTag tag) : tag_(std::move(tag)) {}

    // An explicit tag is kept as written even when no catalog ships for it.
    [[nodiscard]] static Result<LanguagePreference> parse(std::string_view setting_value);
    [[nodiscard]] std::string to_setting_value() const;

    [[nodiscard]] bool follows_system() const noexcept { return !tag_.has_value(); }
    [[nodiscard]] const std::optional<LanguageTag>& tag() const noexcept { return tag_; }

    bool operator==(const LanguagePreference&) const = default;

private:
    std::optional<LanguageTag> tag_;
};

}  // namespace reboot::ux
