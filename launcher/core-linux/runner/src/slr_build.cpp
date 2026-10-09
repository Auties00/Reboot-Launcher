#include "reboot/os_linux/runner/slr_build.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "messages.hpp"
#include "runtime_files.hpp"

namespace rb::os_linux::runner {

namespace {

// umu 1.4.4's x86_64 RUNTIME_VERSIONS: Steam app id to runtime directory.
constexpr std::array<std::pair<std::string_view, std::string_view>, 3> kRuntimes{{
    {"1391110", "steamrt2"},
    {"1628350", "steamrt3"},
    {"4183110", "steamrt4"},
}};

std::string_view trim(std::string_view text) {
    constexpr std::string_view kSpace = " \t\r";
    const std::size_t first = text.find_first_not_of(kSpace);
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(kSpace) - first + 1);
}

// The quoted string after `key`, as VDF writes `"key" "value"`.
std::optional<std::string_view> vdf_value(std::string_view vdf, std::string_view key) {
    const std::string quoted_key = '"' + std::string(key) + '"';
    const std::size_t at = vdf.find(quoted_key);
    if (at == std::string_view::npos) return std::nullopt;
    const std::size_t open = vdf.find('"', at + quoted_key.size());
    if (open == std::string_view::npos) return std::nullopt;
    const std::size_t close = vdf.find('"', open + 1);
    if (close == std::string_view::npos) return std::nullopt;
    return vdf.substr(open + 1, close - open - 1);
}

}  // namespace

std::optional<std::string_view> SlrBuild::runtime_for(std::string_view toolmanifest) {
    const auto appid = vdf_value(toolmanifest, "require_tool_appid");
    if (!appid) return std::nullopt;
    for (const auto& [id, runtime] : kRuntimes)
        if (id == *appid) return runtime;
    return std::nullopt;
}

std::optional<std::string> SlrBuild::depot_version(std::string_view versions) {
    while (!versions.empty()) {
        const std::size_t end = std::min(versions.find('\n'), versions.size());
        const std::string_view line = versions.substr(0, end);
        versions.remove_prefix(std::min(end + 1, versions.size()));

        const std::size_t tab = line.find('\t');
        if (line.starts_with('#') || tab == std::string_view::npos || trim(line.substr(0, tab)) != "depot") continue;
        const std::string_view rest = line.substr(tab + 1);
        const std::string_view version = trim(rest.substr(0, rest.find('\t')));
        if (!version.empty()) return std::string(version);
    }
    return std::nullopt;
}

Result<SlrBuild> SlrBuild::read(const NativePath& proton_root, const NativePath& folders) {
    const auto toolmanifest = read_text(proton_root / "toolmanifest.vdf");
    if (!toolmanifest) return std::unexpected(toolmanifest.error());
    const auto runtime = runtime_for(*toolmanifest);
    if (!runtime) return make_diag(ErrorDomain::Platform, kSlrRuntimeUnknown).arg("path", proton_root).fail();

    const NativePath versions_file = folders / *runtime / "VERSIONS.txt";
    const auto versions = read_text(versions_file);
    if (!versions) return std::unexpected(versions.error());
    auto version = depot_version(*versions);
    if (!version) return make_diag(ErrorDomain::Platform, kSlrBuildMissing).arg("path", versions_file).fail();
    return SlrBuild{std::string(*runtime), std::move(*version)};
}

}  // namespace rb::os_linux::runner
