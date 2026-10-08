#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/ux/language_tag.hpp"

namespace reboot::ux {

// Capabilities: localization.strings-and-language.
// MessageId -> ICU MF1 template for one language; v1 has only the English one.
class MessageCatalog {
public:
    // The English templates of every REBOOT_MESSAGE linked into this program.
    [[nodiscard]] static MessageCatalog english_from_registry();

    [[nodiscard]] const LanguageTag& language() const noexcept { return language_; }
    [[nodiscard]] std::optional<std::string_view> text(std::string_view id) const;

private:
    MessageCatalog(LanguageTag language, std::vector<std::pair<std::string, std::string>> sorted_entries)
        : language_(std::move(language)), entries_(std::move(sorted_entries)) {}

    LanguageTag language_;
    std::vector<std::pair<std::string, std::string>> entries_;
};

}  // namespace reboot::ux
