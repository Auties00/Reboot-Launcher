// Writes and checks l10n/en.json; check with a root also requires every id named in core sources to be registered.
#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/ux/message_catalog_export.hpp"

namespace {

namespace fs = std::filesystem;
using reboot::ux::CatalogIssue;
using reboot::ux::CatalogIssueKind;

[[nodiscard]] std::string_view kind_name(CatalogIssueKind kind) {
    switch (kind) {
        case CatalogIssueKind::MissingId: return "missing";
        case CatalogIssueKind::OrphanedId: return "orphaned";
        case CatalogIssueKind::PlaceholderMismatch: return "placeholder_mismatch";
        case CatalogIssueKind::UnsupportedPlural: return "unsupported_plural";
        case CatalogIssueKind::MalformedTemplate: return "malformed_template";
        case CatalogIssueKind::ConflictingDeclarations: return "conflicting_declarations";
    }
    return "unknown";
}

[[nodiscard]] std::optional<std::string> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream text;
    text << in.rdbuf();
    return std::move(text).str();
}

void print(const reboot::Diagnostic& diag) {
    std::cerr << diag.id;
    if (diag.detail) std::cerr << ": " << *diag.detail;
    std::cerr << '\n';
}

// Core-owned ids written as MessageId{"..."} literals or declared by a REBOOT_MESSAGE(ident, "...", ...) that the
// tool may not have compiled; whitespace is allowed around the punctuation.
[[nodiscard]] std::vector<std::string> literal_ids(std::string_view text) {
    std::vector<std::string> ids;
    const auto scan = [&](std::string_view token, bool declaration) {
        for (std::size_t at = text.find(token); at != std::string_view::npos; at = text.find(token, at + 1)) {
            std::size_t i = at + token.size();
            const auto skip_space = [&] {
                while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r'))
                    ++i;
            };
            skip_space();
            if (i >= text.size() || text[i] != (declaration ? '(' : '{')) continue;
            ++i;
            skip_space();
            if (declaration) {
                while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_')) ++i;
                skip_space();
                if (i >= text.size() || text[i] != ',') continue;
                ++i;
                skip_space();
            }
            if (i >= text.size() || text[i] != '"') continue;
            const std::size_t end = text.find('"', i + 1);
            if (end == std::string_view::npos) continue;
            const std::string_view id = text.substr(i + 1, end - i - 1);
            if (reboot::domain_from_id(id) != reboot::ErrorDomain::Unknown && id.find('.') != std::string_view::npos)
                ids.emplace_back(id);
        }
    };
    scan("MessageId", false);
    scan("REBOOT_MESSAGE", true);
    return ids;
}

// Test sources and core/testing may name ids that only fakes use.
[[nodiscard]] bool scanned(const fs::path& relative) {
    for (const fs::path& part : relative)
        if (part == "tests" || part == "testing") return false;
    const fs::path extension = relative.extension();
    return extension == ".cpp" || extension == ".hpp" || extension == ".h" || extension == ".mm";
}

[[nodiscard]] int undeclared_literals(const fs::path& root) {
    int count = 0;
    for (const fs::directory_entry& top : fs::directory_iterator(root)) {
        if (!top.is_directory() || !top.path().filename().string().starts_with("core")) continue;
        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(top.path())) {
            if (!entry.is_regular_file() || !scanned(fs::relative(entry.path(), root))) continue;
            const std::optional<std::string> text = read_file(entry.path());
            if (!text) continue;
            for (const std::string& id : literal_ids(*text)) {
                const auto registry = reboot::message_registry();
                if (std::ranges::any_of(registry, [&id](const reboot::MessageSpec* spec) { return spec->id == id; }))
                    continue;
                std::cout << "unregistered " << id << ": used in " << fs::relative(entry.path(), root).generic_string()
                          << '\n';
                ++count;
            }
        }
    }
    return count;
}

[[nodiscard]] int run(const std::vector<std::string_view>& args) {
    if (args.size() < 2 || (args[0] != "render" && args[0] != "check") || args.size() > 3 ||
        (args[0] == "render" && args.size() != 2)) {
        std::cerr << "usage: reboot-l10n render <catalog.json>\n       reboot-l10n check <catalog.json> [<root>]\n";
        return 2;
    }
    const reboot::ux::MessageCatalogExport exporter(reboot::message_registry());
    const fs::path catalog_path{std::string(args[1])};
    const std::optional<std::string> existing = read_file(catalog_path);

    if (args[0] == "render") {
        const reboot::Result<std::string> rendered =
            exporter.render(existing ? std::optional<std::string_view>(*existing) : std::nullopt);
        if (!rendered) {
            print(rendered.error());
            return 2;
        }
        std::ofstream out(catalog_path, std::ios::binary | std::ios::trunc);
        out << *rendered;
        if (!out.flush()) {
            std::cerr << "cannot write " << catalog_path.generic_string() << '\n';
            return 2;
        }
        return 0;
    }

    if (!existing) {
        std::cerr << "cannot read " << catalog_path.generic_string() << '\n';
        return 2;
    }
    const reboot::Result<std::vector<CatalogIssue>> issues = exporter.check(*existing);
    if (!issues) {
        print(issues.error());
        return 2;
    }
    for (const CatalogIssue& issue : *issues) {
        std::cout << kind_name(issue.kind) << ' ' << issue.id;
        if (!issue.detail.empty()) std::cout << ": " << issue.detail;
        std::cout << '\n';
    }
    const int undeclared = args.size() == 3 ? undeclared_literals(fs::path{std::string(args[2])}) : 0;
    if (!issues->empty())
        std::cout << "run reboot-l10n render " << catalog_path.generic_string() << " after adding or removing messages\n";
    return issues->empty() && undeclared == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(std::vector<std::string_view>(argv + 1, argv + argc));
    } catch (const std::exception& e) {
        std::cerr << "internal.bug: " << e.what() << '\n';
        return 2;
    }
}
