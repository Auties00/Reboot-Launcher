#include "reboot/ux/settings_search.hpp"

#include <algorithm>
#include <array>

#include "ascii.hpp"

namespace reboot::ux {

namespace {

// Per query token: whole-token matches beat prefix matches, and the field's first token earns a bonus.
constexpr u32 kExactPoints = 4;
constexpr u32 kPrefixPoints = 2;
constexpr u32 kLeadingBonus = 1;

// Folds ASCII case only and splits on ASCII punctuation and space; other bytes stay inside tokens.
[[nodiscard]] std::vector<std::string> tokenize(std::string_view text) {
    std::vector<std::string> tokens;
    std::string current;
    for (const char c : text) {
        if (is_alnum(c) || static_cast<unsigned char>(c) >= 0x80) {
            current.push_back(to_lower(c));
        } else if (!current.empty()) {
            tokens.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) tokens.push_back(std::move(current));
    return tokens;
}

// Every query token must prefix some field token; 0 when one does not.
[[nodiscard]] u32 field_score(const std::vector<std::string>& query, const std::vector<std::string>& field) {
    u32 total = 0;
    for (const std::string& q : query) {
        u32 best = 0;
        for (std::size_t i = 0; i < field.size(); ++i) {
            if (!field[i].starts_with(q)) continue;
            const u32 points = (field[i].size() == q.size() ? kExactPoints : kPrefixPoints) + (i == 0 ? kLeadingBonus : 0);
            best = std::max(best, points);
        }
        if (best == 0) return 0;
        total += best;
    }
    return total;
}

}  // namespace

std::vector<SettingMatch> SettingsSearch::search(std::string_view query, const MessageCatalog& catalog,
                                                 std::size_t limit) const {
    const std::vector<std::string> query_tokens = tokenize(query);
    std::vector<SettingMatch> matches;
    if (query_tokens.empty() || limit == 0) return matches;

    for (const SearchableSetting& setting : settings_) {
        // Title outranks key, which outranks description; equal scores prefer the earlier field.
        struct Field {
            MatchField field;
            u32 weight;
            std::optional<std::string_view> text;
        };
        const std::array<Field, 3> fields{
            Field{MatchField::Title, 3, catalog.text(setting.title.id)},
            Field{MatchField::Key, 2, std::string_view(setting.key)},
            Field{MatchField::Description, 1,
                  setting.description ? catalog.text(setting.description->id) : std::nullopt},
        };
        SettingMatch best{.key = setting.key, .group = setting.group};
        for (const Field& field : fields) {
            if (!field.text) continue;
            const u32 score = field_score(query_tokens, tokenize(*field.text)) * field.weight;
            if (score > best.score) {
                best.field = field.field;
                best.score = score;
            }
        }
        if (best.score > 0) matches.push_back(std::move(best));
    }

    std::ranges::stable_sort(matches, std::ranges::greater{}, &SettingMatch::score);
    if (matches.size() > limit) matches.resize(limit);
    return matches;
}

}  // namespace reboot::ux
