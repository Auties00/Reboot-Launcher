#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ux/message_catalog.hpp"

namespace reboot::ux {

// One settings registry key as search sees it; the engine builds these from storage's registry.
struct SearchableSetting {
    std::string key;
    std::string group;
    MessageId title;
    std::optional<MessageId> description;
};

enum class MatchField : u8 { Key, Title, Description };

struct SettingMatch {
    std::string key;
    std::string group;
    MatchField field{};
    u32 score = 0;
};

// Capabilities: onboarding-ux-flows.settings-search, linux-gui-toolkit.
// One ranking for every UI and the CLI: token-prefix scoring of key, title, description; ties keep registry order.
class SettingsSearch {
public:
    explicit SettingsSearch(std::vector<SearchableSetting> settings) : settings_(std::move(settings)) {}

    // An empty or all-space query matches nothing.
    [[nodiscard]] std::vector<SettingMatch> search(std::string_view query, const MessageCatalog& catalog,
                                                   std::size_t limit) const;

private:
    std::vector<SearchableSetting> settings_;
};

}  // namespace reboot::ux
