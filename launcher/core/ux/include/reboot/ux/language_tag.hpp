#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "reboot/foundation/diag.hpp"

namespace reboot::ux {

// Capabilities: localization.strings-and-language.
// A well-formed BCP 47 tag in canonical case ("en", "pt-BR", "zh-Hant-TW").
class LanguageTag {
public:
    // Rejects anything that is not well-formed; does not check that the language exists.
    [[nodiscard]] static Result<LanguageTag> parse(std::string_view text);

    // Accepts OS forms too: "de_DE.UTF-8", "sr_RS@latin", "de-DE". "C" and "POSIX" give nullopt.
    [[nodiscard]] static std::optional<LanguageTag> from_os_locale(std::string_view locale);

    [[nodiscard]] static LanguageTag english() { return LanguageTag("en"); }

    [[nodiscard]] const std::string& str() const noexcept { return text_; }
    [[nodiscard]] std::string_view primary_language() const noexcept {
        return std::string_view(text_).substr(0, text_.find('-'));
    }

    bool operator==(const LanguageTag&) const = default;

private:
    explicit LanguageTag(std::string text) : text_(std::move(text)) {}

    std::string text_;
};

}  // namespace reboot::ux
