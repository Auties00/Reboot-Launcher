#include "reboot/integration/entry_command.hpp"

#include <cstddef>
#include <utility>

namespace reboot::integration {

std::optional<std::string_view> url_placeholder(EntryFlavor flavor) noexcept {
    switch (flavor) {
        case EntryFlavor::Windows: return "%1";
        case EntryFlavor::FreeDesktop: return "%u";
        case EntryFlavor::Apple: return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::vector<std::string>> expected_args(IntegrationKind kind, EntryFlavor flavor) {
    const std::optional<std::string_view> placeholder = url_placeholder(flavor);
    if (!placeholder) return std::nullopt;
    switch (kind) {
        case IntegrationKind::UrlScheme:
            return std::vector<std::string>{std::string(kActivateUrlFlag), std::string(*placeholder)};
        case IntegrationKind::Autostart:
            return std::vector<std::string>{std::string(kRunVerb), std::string(kServiceManagerOrigin)};
        case IntegrationKind::EngineAgent:
        case IntegrationKind::DesktopEntry: return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::vector<std::string>> split_entry_command(std::string_view text) {
    std::vector<std::string> tokens;
    std::string token;
    bool in_token = false;
    bool quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\\' && i + 1 < text.size() && text[i + 1] == '"') {
            token += '"';
            in_token = true;
            ++i;
        } else if (c == '"') {
            quoted = !quoted;
            in_token = true;
        } else if (c == ' ' && !quoted) {
            if (in_token) tokens.push_back(std::exchange(token, {}));
            in_token = false;
        } else {
            token += c;
            in_token = true;
        }
    }
    if (quoted) return std::nullopt;
    if (in_token) tokens.push_back(std::move(token));
    if (tokens.empty()) return std::nullopt;
    return tokens;
}

}  // namespace reboot::integration
