// Writes and checks l10n/en.json and the SDK's English catalog; check with a root also requires every
// id named in core and payload sources to be registered.
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
using rb::ux::CatalogIssue;
using rb::ux::CatalogIssueKind;

[[nodiscard]] std::string_view kind_name(CatalogIssueKind kind) {
    switch (kind) {
        case CatalogIssueKind::MissingId: return "missing";
        case CatalogIssueKind::OrphanedId: return "orphaned";
        case CatalogIssueKind::PlaceholderMismatch: return "placeholder_mismatch";
        case CatalogIssueKind::UnsupportedPlural: return "unsupported_plural";
        case CatalogIssueKind::MalformedTemplate: return "malformed_template";
        case CatalogIssueKind::ConflictingDeclarations: return "conflicting_declarations";
        case CatalogIssueKind::StaleTemplate: return "stale_template";
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

void print(const rb::Diagnostic& diag) {
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
            if (rb::domain_from_id(id) != rb::ErrorDomain::Unknown && id.find('.') != std::string_view::npos)
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
        const std::string name = top.path().filename().string();
        if (!top.is_directory() || (!name.starts_with("core") && name != "payload")) continue;
        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(top.path())) {
            if (!entry.is_regular_file() || !scanned(fs::relative(entry.path(), root))) continue;
            const std::optional<std::string> text = read_file(entry.path());
            if (!text) continue;
            for (const std::string& id : literal_ids(*text)) {
                const auto registry = rb::message_registry();
                if (std::ranges::any_of(registry, [&id](const rb::MessageSpec* spec) { return spec->id == id; }))
                    continue;
                std::cout << "unregistered " << id << ": used in " << fs::relative(entry.path(), root).generic_string()
                          << '\n';
                ++count;
            }
        }
    }
    return count;
}

// Relative to the launcher root: the English templates rb::sdk::Error renders, for clients that have
// no message registry.
constexpr std::string_view kSdkCatalog = "core/sdk/include/reboot/sdk/english_catalog.hpp";

// Every byte outside printable ASCII as an octal escape, so the header reads the same in any source
// charset; octal stops after three digits, unlike \x.
[[nodiscard]] std::string cpp_literal(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (byte < 0x20 || byte >= 0x7F) {
            out += '\\';
            out += static_cast<char>('0' + ((byte >> 6) & 7));
            out += static_cast<char>('0' + ((byte >> 3) & 7));
            out += static_cast<char>('0' + (byte & 7));
        } else {
            out += c;
        }
    }
    out += '"';
    return out;
}

[[nodiscard]] std::string sdk_catalog() {
    std::vector<const rb::MessageSpec*> specs;
    for (const rb::MessageSpec* spec : rb::message_registry())
        if (specs.empty() || specs.back()->id != spec->id) specs.push_back(spec);
    std::string out =
        "// Generated by reboot-l10n render from the message registry. Do not edit.\n"
        "#pragma once\n\n#include <array>\n#include <string_view>\n\nnamespace rb::sdk::detail {\n\n"
        "struct EnglishTemplate {\n    std::string_view id;\n    std::string_view text;\n};\n\n"
        "// Every message the engine and reboot_client report, sorted by id.\n";
    out += "inline constexpr std::array<EnglishTemplate, " + std::to_string(specs.size()) + "> kEnglish{{\n";
    for (const rb::MessageSpec* spec : specs)
        out += "    {" + cpp_literal(spec->id) + ", " + cpp_literal(spec->english) + "},\n";
    out += "}};\n\n}  // namespace rb::sdk::detail\n";
    return out;
}

[[nodiscard]] bool write_file(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    if (out.flush()) return true;
    std::cerr << "cannot write " << path.generic_string() << '\n';
    return false;
}

[[nodiscard]] int run(const std::vector<std::string_view>& args) {
    if (args.size() < 2 || (args[0] != "render" && args[0] != "check") || args.size() > 3) {
        std::cerr << "usage: reboot-l10n render <catalog.json> [<root>]\n       reboot-l10n check <catalog.json> [<root>]\n";
        return 2;
    }
    const std::optional<fs::path> root =
        args.size() == 3 ? std::optional<fs::path>(fs::path{std::string(args[2])}) : std::nullopt;
    const rb::ux::MessageCatalogExport exporter(rb::message_registry());
    const fs::path catalog_path{std::string(args[1])};
    const std::optional<std::string> existing = read_file(catalog_path);

    if (args[0] == "render") {
        const rb::Result<std::string> rendered =
            exporter.render(existing ? std::optional<std::string_view>(*existing) : std::nullopt);
        if (!rendered) {
            print(rendered.error());
            return 2;
        }
        if (!write_file(catalog_path, *rendered)) return 2;
        if (root && !write_file(*root / kSdkCatalog, sdk_catalog())) return 2;
        return 0;
    }

    if (!existing) {
        std::cerr << "cannot read " << catalog_path.generic_string() << '\n';
        return 2;
    }
    const rb::Result<std::vector<CatalogIssue>> issues = exporter.check(*existing);
    if (!issues) {
        print(issues.error());
        return 2;
    }
    for (const CatalogIssue& issue : *issues) {
        std::cout << kind_name(issue.kind) << ' ' << issue.id;
        if (!issue.detail.empty()) std::cout << ": " << issue.detail;
        std::cout << '\n';
    }
    const int undeclared = root ? undeclared_literals(*root) : 0;
    const bool sdk_current = !root || read_file(*root / kSdkCatalog) == sdk_catalog();
    if (!sdk_current) std::cout << "stale " << kSdkCatalog << '\n';
    if (!issues->empty() || !sdk_current)
        std::cout << "run reboot-l10n render " << catalog_path.generic_string()
                  << " <launcher root> after adding, removing or rewording messages\n";
    return issues->empty() && undeclared == 0 && sdk_current ? 0 : 1;
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
