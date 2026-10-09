#include "reboot/builds/layout_resolver.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "builds_error.hpp"
#include "messages.hpp"
#include "path_text.hpp"
#include "reboot/foundation/text.hpp"

namespace rb::builds {

namespace {

namespace fs = std::filesystem;

// FortniteGame/Binaries/Win64, the folder the shipping exe ships in.
constexpr std::array<std::string_view, 3> kGameBinaries{"FortniteGame", "Binaries", "Win64"};
constexpr std::array<std::string_view, 3> kEngineBinaries{"Engine", "Binaries", "Win64"};

[[nodiscard]] Diagnostic error(BuildsErrorCode code, const NativePath& path) {
    return to_diagnostic(BuildsError{.code = code, .path = path});
}

[[nodiscard]] bool is_file(const NativePath& path) {
    std::error_code ec;
    return fs::is_regular_file(fs::symlink_status(path, ec));
}

[[nodiscard]] bool is_dir(const NativePath& path) {
    std::error_code ec;
    return fs::is_directory(fs::symlink_status(path, ec));
}

// True when the last three elements of `dir` are `tail`, ignoring ASCII case.
[[nodiscard]] bool ends_with(const NativePath& dir, const std::array<std::string_view, 3>& tail) {
    std::vector<NativePath> parts;
    for (const NativePath& part : dir.lexically_normal())
        if (!part.empty()) parts.push_back(part);
    if (parts.size() < tail.size()) return false;
    for (std::size_t i = 0; i < tail.size(); ++i)
        if (!iequals_ascii(utf8_name(parts[parts.size() - tail.size() + i]), tail[i])) return false;
    return true;
}

[[nodiscard]] NativePath game_binaries() { return NativePath("FortniteGame") / "Binaries" / "Win64"; }

[[nodiscard]] NativePath strip_trailing_separator(const NativePath& path) {
    NativePath normal = path.lexically_normal();
    if (!normal.has_filename() && normal.has_relative_path()) normal = normal.parent_path();
    return normal;
}

// A root the known paths settle without walking: the folder, its one wrapper folder, or the
// root above a FortniteGame/Binaries/Win64 folder that was picked itself.
[[nodiscard]] std::optional<NativePath> probe_root(const NativePath& folder) {
    if (is_file(folder / game_binaries() / kShippingExe)) return folder;

    std::error_code ec;
    std::optional<NativePath> wrapper;
    std::size_t entries = 0;
    for (fs::directory_iterator it(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        ++entries;
        if (is_dir(it->path())) wrapper = it->path();
    }
    if (!ec && entries == 1 && wrapper && is_file(*wrapper / game_binaries() / kShippingExe)) return wrapper;

    if (ends_with(folder, kGameBinaries) && is_file(folder / kShippingExe))
        return folder.parent_path().parent_path().parent_path();
    return std::nullopt;
}

// The build root of a shipping exe at `root`/`exe`: three levels above FortniteGame/Binaries/Win64.
[[nodiscard]] NativePath root_for(const NativePath& walked, const NativePath& exe) {
    const NativePath dir = exe.parent_path();
    if (!ends_with(dir, kGameBinaries)) return walked;
    return walked / dir.parent_path().parent_path().parent_path();
}

[[nodiscard]] bool same_path(const NativePath& a, const NativePath& b) {
    return folded_key(a.lexically_normal()) == folded_key(b.lexically_normal());
}

[[nodiscard]] bool inside(const NativePath& relative) {
    return !relative.empty() && *relative.begin() != NativePath("..");
}

// Indexes into kLayoutTargets.
constexpr std::size_t kShippingIndex = 0;
constexpr std::size_t kLauncherIndex = 1;
constexpr std::size_t kEacIndex = 2;
constexpr std::size_t kCrashReportIndex = 3;
constexpr std::size_t kAftermathIndex = 4;

}  // namespace

Result<LayoutResolution> LayoutResolver::resolve(const NativePath& folder_in,
                                                 const std::optional<NativePath>& chosen_shipping,
                                                 const CancelToken& token) const {
    const NativePath folder = strip_trailing_separator(folder_in);
    std::error_code ec;
    const fs::file_status status = fs::status(folder, ec);
    if (ec || !fs::exists(status)) {
        return std::unexpected(to_diagnostic(
            BuildsError{.code = BuildsErrorCode::Io, .path = folder, .os_error = SystemError{.code = ec.value()}}));
    }
    if (!fs::is_directory(status)) return std::unexpected(error(BuildsErrorCode::NotADirectory, folder));

    const NativePath walked = probe_root(folder).value_or(folder);
    Result<FindResult> walk = finder_.find(walked, kLayoutTargets, token);
    if (!walk) return std::unexpected(std::move(walk.error()));

    // Relative to the walked folder, per kLayoutTargets name.
    std::array<std::vector<NativePath>, kLayoutTargets.size()> found;
    for (const FoundFile& file : walk->files) found[file.name_index].push_back(file.relative);
    const std::vector<NativePath>& shipping = found[kShippingIndex];

    // Candidates and the choice are relative to `folder`, which may lie below the walked root.
    const auto to_folder = [&](const NativePath& relative) { return (walked / relative).lexically_relative(folder); };

    NativePath exe;
    if (chosen_shipping) {
        const auto match = std::ranges::find_if(shipping, [&](const NativePath& candidate) {
            return same_path(to_folder(candidate), *chosen_shipping);
        });
        if (chosen_shipping->is_absolute() || match == shipping.end()) {
            return std::unexpected(to_diagnostic(BuildsError{
                .code = BuildsErrorCode::ShippingNotFound, .path = folder, .other_path = *chosen_shipping}));
        }
        exe = *match;
    } else if (shipping.empty()) {
        // Folders the walk could not read may be where it is.
        std::optional<Diagnostic> cause;
        if (!walk->errors.empty()) {
            cause = make_diag(ErrorDomain::Builds, msg::kWalkIncomplete)
                        .arg("count", walk->errors.size())
                        .arg("path", walked)
                        .severity(Severity::Warning)
                        .build();
        }
        return std::unexpected(
            to_diagnostic(BuildsError{.code = BuildsErrorCode::MissingShipping, .path = folder, .cause = cause}));
    } else if (shipping.size() > 1) {
        NeedsShippingChoice choice{.folder = folder, .candidates = {}};
        for (const NativePath& candidate : shipping) choice.candidates.push_back(to_folder(candidate));
        return LayoutResolution(std::move(choice));
    } else {
        exe = shipping.front();
    }

    const NativePath root = root_for(walked, exe).lexically_normal();
    const auto under_root = [&](const NativePath& relative) { return (walked / relative).lexically_relative(root); };
    const auto collect = [&](std::size_t index) {
        std::vector<NativePath> out;
        for (const NativePath& relative : found[index])
            if (NativePath rebased = under_root(relative); inside(rebased)) out.push_back(std::move(rebased));
        return out;
    };

    BuildLayout layout{.root = strip_trailing_separator(root), .shipping_exe = under_root(exe)};
    const NativePath binaries = layout.shipping_exe.parent_path();
    // The copy beside the shipping exe wins, then the first by path.
    const auto pick = [&](std::size_t index) -> std::optional<NativePath> {
        std::vector<NativePath> candidates = collect(index);
        if (candidates.empty()) return std::nullopt;
        const auto beside = std::ranges::find_if(
            candidates, [&](const NativePath& candidate) { return same_path(candidate.parent_path(), binaries); });
        return beside != candidates.end() ? *beside : candidates.front();
    };
    layout.launcher_exe = pick(kLauncherIndex);
    layout.eac_exe = pick(kEacIndex);
    layout.crash_report_clients = collect(kCrashReportIndex);
    std::ranges::stable_partition(layout.crash_report_clients, [](const NativePath& path) {
        return ends_with(path.parent_path(), kEngineBinaries);
    });
    layout.aftermath_dlls = collect(kAftermathIndex);
    return LayoutResolution(std::move(layout));
}

bool LayoutResolver::still_valid(const BuildLayout& layout) const {
    if (!is_dir(layout.root) || !is_file(layout.root / layout.shipping_exe)) return false;
    for (const std::optional<NativePath>& optional : {layout.launcher_exe, layout.eac_exe})
        if (optional && !is_file(layout.root / *optional)) return false;
    for (const std::vector<NativePath>* paths : {&layout.crash_report_clients, &layout.aftermath_dlls})
        for (const NativePath& path : *paths)
            if (!is_file(layout.root / path)) return false;
    return true;
}

}  // namespace rb::builds
