#include "reboot/ux/language_preference.hpp"

#include "ascii.hpp"

namespace rb::ux {

Result<LanguagePreference> LanguagePreference::parse(std::string_view setting_value) {
    // "System" would otherwise parse as the well-formed language subtag "system".
    if (iequals(setting_value, kSystemValue)) return LanguagePreference{};
    Result<LanguageTag> tag = LanguageTag::parse(setting_value);
    if (!tag) return std::unexpected(std::move(tag.error()));
    return LanguagePreference(std::move(*tag));
}

std::string LanguagePreference::to_setting_value() const {
    return tag_ ? tag_->str() : std::string(kSystemValue);
}

}  // namespace rb::ux
