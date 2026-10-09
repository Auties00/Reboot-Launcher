#include "prefix_files.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/compat/pe_imports.hpp"
#include "reboot/compat/preferred_rhi_seed.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/ports/file_system.hpp"

namespace rb::compat {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kPublicProfile = "Public";

[[nodiscard]] std::string name_of(const NativePath& path) {
    const std::u8string text = path.filename().u8string();
    return {text.begin(), text.end()};
}

[[nodiscard]] SystemError host_error(const std::error_code& error) {
    return SystemError{SystemError::Origin::Host, error.value()};
}

[[nodiscard]] std::unexpected<Diagnostic> step_failed(RunnerKind kind, std::string_view step, const std::error_code& error) {
    Diagnostic diag = prefix_failed(kind, step);
    diag.os_error = host_error(error);
    return std::unexpected(std::move(diag));
}

[[nodiscard]] std::unexpected<Diagnostic> step_failed(RunnerKind kind, std::string_view step, Diagnostic cause) {
    Diagnostic diag = prefix_failed(kind, step);
    diag.causes.push_back(std::move(cause));
    return std::unexpected(std::move(diag));
}

[[nodiscard]] bool is_real_dir(const NativePath& path) {
    std::error_code error;
    return fs::is_directory(fs::symlink_status(path, error));
}

// Directory entries sorted by name, so every choice among them is stable.
[[nodiscard]] std::vector<NativePath> children(const NativePath& dir) {
    std::vector<NativePath> out;
    std::error_code error;
    for (fs::directory_iterator it(dir, error); !error && it != fs::directory_iterator(); it.increment(error))
        out.push_back(it->path());
    std::ranges::sort(out);
    return out;
}

[[nodiscard]] std::vector<NativePath> user_dirs(const NativePath& prefix) {
    std::vector<NativePath> out;
    for (NativePath& dir : children(prefix / "drive_c" / "users"))
        if (is_real_dir(dir) && !iequals_ascii(name_of(dir), kPublicProfile)) out.push_back(std::move(dir));
    return out;
}

[[nodiscard]] NativePath saved_dir(const NativePath& user) {
    return user / "AppData" / "Local" / "FortniteGame" / "Saved";
}

[[nodiscard]] std::optional<NativePath> find_saved(const NativePath& tree) {
    for (const NativePath& user : user_dirs(tree))
        if (is_real_dir(saved_dir(user))) return saved_dir(user);
    return std::nullopt;
}

// Windows names compare case-insensitively, so an import may spell a neighbour differently.
[[nodiscard]] std::optional<NativePath> neighbour(const std::vector<NativePath>& dir, std::string_view import) {
    for (const NativePath& entry : dir)
        if (iequals_ascii(name_of(entry), import)) return entry;
    return std::nullopt;
}

[[nodiscard]] Result<bool> imports_need_vc(ports::IFileSystem& files, const NativePath& dll,
                                           std::vector<std::string>& imports) {
    auto bytes = files.read_all(dll);
    if (!bytes) return std::unexpected(std::move(bytes.error()));
    auto read = read_pe_imports(dll, *bytes);
    if (!read) return std::unexpected(std::move(read.error()));
    imports = std::move(*read);
    return needs_vc_runtime(imports);
}

}  // namespace

Diagnostic prefix_failed(RunnerKind kind, std::string_view step) {
    return make_diag(ErrorDomain::Compat, msg::kPrefixFailed).arg("runner", runner_name(kind)).arg("step", step).build();
}

PrefixState inspect_prefix(const NativePath& prefix) {
    std::error_code error;
    const fs::file_status status = fs::symlink_status(prefix, error);
    if (status.type() == fs::file_type::not_found) return PrefixState::Missing;
    if (error || !fs::is_directory(status)) return PrefixState::Unusable;
    const bool drive_c = fs::is_directory(prefix / "drive_c", error);
    const bool system_reg = fs::is_regular_file(prefix / "system.reg", error);
    return drive_c && system_reg ? PrefixState::Usable : PrefixState::Unusable;
}

Result<bool> game_needs_vc_runtime(ports::IFileSystem& files, std::span<const NativePath> game_dlls) {
    std::vector<NativePath> scanned(game_dlls.begin(), game_dlls.end());
    for (const NativePath& dll : game_dlls) {
        std::vector<std::string> imports;
        const auto needs = imports_need_vc(files, dll, imports);
        if (!needs) return std::unexpected(needs.error());
        if (*needs) return true;

        const std::vector<NativePath> beside = children(dll.parent_path());
        for (const std::string& import : imports) {
            const auto found = neighbour(beside, import);
            if (!found || std::ranges::find(scanned, *found) != scanned.end()) continue;
            scanned.push_back(*found);
            std::vector<std::string> nested;
            const auto nested_needs = imports_need_vc(files, *found, nested);
            if (nested_needs && *nested_needs) return true;
        }
    }
    return false;
}

NativePath backup_path(const NativePath& prefixes_dir, RunnerKind kind, std::string_view version) {
    std::string name = std::string(runner_name(kind)) + ".backup-";
    for (const char c : version) name += c == '/' || c == '\\' || c == ':' ? '_' : c;
    return prefixes_dir / name;
}

Result<void> back_up_prefix(ports::IFileSystem& files, const NativePath& prefixes_dir, RunnerKind kind,
                            const NativePath& prefix, std::string_view version) {
    const auto failed = [&] {
        return make_diag(ErrorDomain::Compat, msg::kPrefixBackupFailed).arg("runner", runner_name(kind));
    };
    // The copy lands beside the earlier backup first, so a failed copy still leaves that one.
    const NativePath partial = prefixes_dir / (std::string(runner_name(kind)) + ".backup.partial");
    if (auto removed = files.remove_tree(partial); !removed) return failed().cause(std::move(removed.error())).fail();
    std::error_code error;
    fs::copy(prefix, partial, fs::copy_options::recursive | fs::copy_options::copy_symlinks, error);
    if (error) {
        static_cast<void>(files.remove_tree(partial));
        return failed().os(host_error(error)).fail();
    }

    const std::string earlier = std::string(runner_name(kind)) + ".backup-";
    for (const NativePath& entry : children(prefixes_dir)) {
        if (!name_of(entry).starts_with(earlier)) continue;
        if (auto removed = files.remove_tree(entry); !removed) {
            static_cast<void>(files.remove_tree(partial));
            return failed().cause(std::move(removed.error())).fail();
        }
    }
    fs::rename(partial, backup_path(prefixes_dir, kind, version), error);
    if (!error) return {};
    static_cast<void>(files.remove_tree(partial));
    return failed().os(host_error(error)).fail();
}

NativePath aside_path(const NativePath& prefixes_dir, RunnerKind kind) {
    return prefixes_dir / (std::string(runner_name(kind)) + ".old");
}

Result<void> set_aside(ports::IFileSystem& files, RunnerKind kind, const NativePath& prefix, const NativePath& aside) {
    std::error_code error;
    // An aside left by an earlier attempt still holds the saved data unless this tree has its own.
    if (fs::exists(fs::symlink_status(aside, error)) && !holds_saved(prefix)) {
        if (auto removed = files.remove_tree(prefix); !removed) return step_failed(kind, kStepSetAside, std::move(removed.error()));
        return {};
    }
    if (auto removed = files.remove_tree(aside); !removed) return step_failed(kind, kStepSetAside, std::move(removed.error()));
    fs::rename(prefix, aside, error);
    if (error) return step_failed(kind, kStepSetAside, error);
    return {};
}

Result<void> carry_saved(ports::IFileSystem& files, RunnerKind kind, const NativePath& old_tree,
                         const NativePath& prefix) {
    if (const std::optional<NativePath> saved = find_saved(old_tree)) {
        const auto user = prefix_user_dir(prefix);
        if (!user) return std::unexpected(prefix_failed(kind, kStepCarrySaved));
        const NativePath target = saved_dir(*user);
        std::error_code error;
        if (!fs::exists(fs::symlink_status(target, error))) {
            fs::create_directories(target.parent_path(), error);
            if (!error) fs::rename(*saved, target, error);
            if (error) return step_failed(kind, kStepCarrySaved, error);
        }
    }
    // The new prefix is ready either way; a leftover tree goes with the next set_aside().
    if (auto removed = files.remove_tree(old_tree); !removed)
        REBOOT_LOG_WARN(Play, "the old Wine prefix of the {} runner stays behind: {}", runner_name(kind), removed.error().id);
    return {};
}

bool holds_saved(const NativePath& tree) { return find_saved(tree).has_value(); }

std::optional<NativePath> prefix_user_dir(const NativePath& prefix) {
    std::vector<NativePath> users = user_dirs(prefix);
    if (users.empty()) return std::nullopt;
    return std::move(users.front());
}

Result<void> seed_rhi(ports::IFileSystem& files, RunnerKind kind, const NativePath& prefix) {
    const auto user = prefix_user_dir(prefix);
    if (!user) return std::unexpected(prefix_failed(kind, kStepSeedRhi));
    const NativePath ini = saved_dir(*user) / "Config" / "WindowsClient" / kGameUserSettingsFile;

    std::string text;
    std::error_code error;
    if (fs::exists(ini, error)) {
        auto bytes = files.read_all(ini);
        if (!bytes) return step_failed(kind, kStepSeedRhi, std::move(bytes.error()));
        text.assign(bytes->begin(), bytes->end());
    }
    const std::optional<std::string> seeded = seed_preferred_rhi(text);
    if (!seeded) return {};
    fs::create_directories(ini.parent_path(), error);
    if (error) return step_failed(kind, kStepSeedRhi, error);
    const std::span<const u8> bytes(reinterpret_cast<const u8*>(seeded->data()), seeded->size());
    if (auto written = files.atomic_replace(ini, bytes, false); !written)
        return step_failed(kind, kStepSeedRhi, std::move(written.error()));
    return {};
}

}  // namespace rb::compat
