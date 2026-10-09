#include "reboot/ux/message_catalog_export.hpp"

#include <algorithm>
#include <tuple>
#include <utility>

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>

#include "message_template.hpp"
#include "messages.hpp"

namespace rb::ux {

namespace {

constexpr std::string_view kLocaleKey = "@@locale";

[[nodiscard]] Diagnostic malformed_catalog(std::string detail) {
    return make_diag(ErrorDomain::Ux, msg::kMalformedCatalog)
        .arg("language", "en")
        .detail(std::move(detail))
        .kind(ErrorKind::InvalidInput);
}

[[nodiscard]] Result<boost::json::object> parse_catalog(std::string_view text) {
    boost::system::error_code ec;
    boost::json::value value = boost::json::parse(text, ec);
    if (ec) return std::unexpected(malformed_catalog(ec.message()));
    if (!value.is_object()) return std::unexpected(malformed_catalog("the catalog is not a JSON object"));
    return std::move(value.as_object());
}

// ARB metadata ("@key", "@@locale") is not a message.
[[nodiscard]] bool is_metadata(std::string_view key) { return key.starts_with('@'); }

// "@@locale" first, then by message key with each "@key" right after its key.
[[nodiscard]] auto sort_key(std::string_view key) {
    if (key.starts_with("@@")) return std::tuple(0, key, false);
    if (is_metadata(key)) return std::tuple(1, key.substr(1), true);
    return std::tuple(1, key, false);
}

[[nodiscard]] std::string join_names(const std::vector<std::string_view>& names) {
    std::string out = "{";
    for (const std::string_view name : names) {
        if (out.size() > 1) out += ", ";
        out += name;
    }
    return out + "}";
}

}  // namespace

bool is_core_owned_key(std::string_view key) {
    return key.find('.') != std::string_view::npos && domain_from_id(key) != ErrorDomain::Unknown;
}

Result<std::string> MessageCatalogExport::render(std::optional<std::string_view> existing_catalog) const {
    const auto in_registry = [this](std::string_view key) {
        return std::ranges::any_of(registry_, [key](const MessageSpec* spec) { return spec->id == key; });
    };

    std::vector<std::pair<std::string, boost::json::value>> entries;
    entries.emplace_back(kLocaleKey, "en");
    for (const MessageSpec* spec : registry_)
        if (std::ranges::none_of(entries, [spec](const auto& entry) { return entry.first == spec->id; }))
            entries.emplace_back(spec->id, boost::json::string(spec->english));

    if (existing_catalog) {
        Result<boost::json::object> existing = parse_catalog(*existing_catalog);
        if (!existing) return std::unexpected(std::move(existing.error()));
        const auto keeps_message = [&](std::string_view key) {
            return in_registry(key) || (!is_core_owned_key(key) && existing->contains(key));
        };
        for (const auto& member : *existing) {
            const std::string_view key = member.key();
            if (key == kLocaleKey) continue;
            const bool keep = is_metadata(key) ? keeps_message(key.substr(1)) : !in_registry(key) && !is_core_owned_key(key);
            if (keep) entries.emplace_back(key, member.value());
        }
    }

    std::ranges::sort(entries, [](const auto& a, const auto& b) { return sort_key(a.first) < sort_key(b.first); });
    std::string out = "{\n";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        out += "  ";
        out += boost::json::serialize(boost::json::value(boost::json::string(entries[i].first)));
        out += ": ";
        out += boost::json::serialize(entries[i].second);
        out += i + 1 < entries.size() ? ",\n" : "\n";
    }
    out += "}\n";
    return out;
}

Result<std::vector<CatalogIssue>> MessageCatalogExport::check(std::string_view catalog_json) const {
    Result<boost::json::object> catalog = parse_catalog(catalog_json);
    if (!catalog) return std::unexpected(std::move(catalog.error()));

    std::vector<CatalogIssue> issues;
    for (const MessageSpec* spec : registry_)
        if (!catalog->contains(spec->id) &&
            std::ranges::none_of(issues, [spec](const CatalogIssue& issue) { return issue.id == spec->id; }))
            issues.push_back({CatalogIssueKind::MissingId, std::string(spec->id), {}});

    // The catalog holds one template per id, so every declaration of an id must agree on it.
    std::vector<const MessageSpec*> by_id(registry_.begin(), registry_.end());
    std::ranges::stable_sort(by_id, {}, &MessageSpec::id);
    for (std::size_t begin = 0; begin < by_id.size();) {
        const MessageSpec& first = *by_id[begin];
        std::size_t end = begin + 1;
        while (end < by_id.size() && by_id[end]->id == first.id) ++end;
        for (std::size_t j = begin + 1; j < end; ++j) {
            if (by_id[j]->english == first.english) continue;
            issues.push_back({CatalogIssueKind::ConflictingDeclarations, std::string(first.id),
                              "\"" + std::string(first.english) + "\" and \"" + std::string(by_id[j]->english) + "\""});
            break;
        }
        begin = end;
    }

    for (const auto& member : *catalog) {
        const std::string_view key = member.key();
        if (is_metadata(key)) continue;
        const bool registered = std::ranges::any_of(registry_, [key](const MessageSpec* s) { return s->id == key; });
        if (!registered && is_core_owned_key(key)) {
            issues.push_back({CatalogIssueKind::OrphanedId, std::string(key), {}});
            continue;
        }
        if (!member.value().is_string()) {
            issues.push_back({CatalogIssueKind::MalformedTemplate, std::string(key), "the value is not a string"});
            continue;
        }
        const std::string_view text = member.value().get_string();
        if (text.find_first_not_of(" \t\r\n") == std::string_view::npos) {
            issues.push_back({CatalogIssueKind::MalformedTemplate, std::string(key), "the template is empty"});
            continue;
        }
        const auto parsed = parse_template(text);
        if (!parsed) {
            issues.push_back({CatalogIssueKind::MalformedTemplate, std::string(key), parsed.error()});
            continue;
        }
        for (const std::string& construct : parsed->unsupported)
            issues.push_back({CatalogIssueKind::UnsupportedPlural, std::string(key), construct});
        std::vector<std::string_view> found(parsed->args.begin(), parsed->args.end());
        std::ranges::sort(found);
        // An id declared by several OS modules must match each declaration.
        for (const MessageSpec* spec : registry_) {
            if (spec->id != key) continue;
            std::vector<std::string_view> expected;
            for (const ArgSpec& arg : spec->args) expected.push_back(arg.name);
            std::ranges::sort(expected);
            if (expected == found) continue;
            issues.push_back({CatalogIssueKind::PlaceholderMismatch, std::string(key),
                              "expected " + join_names(expected) + ", found " + join_names(found)});
            break;
        }
    }

    std::ranges::stable_sort(issues, [](const CatalogIssue& a, const CatalogIssue& b) {
        return std::tie(a.id, a.kind) < std::tie(b.id, b.kind);
    });
    return issues;
}

}  // namespace rb::ux
