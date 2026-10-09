#include "reboot/os_linux/runner/umu_invocation.hpp"

#include <algorithm>
#include <filesystem>

#include "messages.hpp"
#include "runtime_files.hpp"

namespace rb::os_linux::runner {

namespace {

// Blocking. The archive root holding `file`, or `missing` as the error.
Result<NativePath> find_executable(const NativePath& dir, std::string_view file, MessageId missing) {
    const auto root = archive_root(dir, file);
    if (!root) return std::unexpected(root.error());
    const auto status = status_of(*root / file);
    if (!status) return std::unexpected(status.error());
    if (!is_executable_file(*status))
        return make_diag(ErrorDomain::Platform, missing).arg("path", dir).kind(ErrorKind::NotFound).fail();
    return *root;
}

}  // namespace

Result<UmuInvocation> UmuInvocation::resolve(const NativePath& launcher_dir, const NativePath& proton_dir,
                                             const NativePath& folders) {
    const auto launcher = find_executable(launcher_dir, kUmuRun, kUmuRunMissing);
    if (!launcher) return std::unexpected(launcher.error());
    const auto proton = find_executable(proton_dir, kProtonScript, kProtonMissing);
    if (!proton) return std::unexpected(proton.error());
    return UmuInvocation{*launcher / kUmuRun, *proton, folders};
}

std::vector<std::pair<std::string, std::string>> UmuInvocation::env() const {
    // string() is the native bytes on POSIX.
    return {
        {"PROTONPATH", proton_root.string()},
        {"GAMEID", std::string(kGameId)},
        {"PROTONFIXES_DISABLE", "1"},
        {"UMU_FOLDERS_PATH", folders.string()},
        {"UMU_RUNTIME_UPDATE", "0"},
    };
}

ports::RuntimeLayout UmuInvocation::to_runtime_layout() const {
    ports::RuntimeLayout out;
    out.root = proton_root;
    out.entry = umu_run;
    out.env = env();
    return out;
}

bool UmuInvocation::is_umu_layout(const ports::RuntimeLayout& layout) {
    return std::ranges::any_of(layout.env, [](const auto& var) { return var.first == "PROTONPATH"; });
}

Result<std::string> UmuInvocation::filesystems_rw(std::string_view inherited, std::span<const NativePath> paths) {
    std::vector<std::string> entries;
    const auto add = [&](std::string entry) {
        if (!entry.empty() && std::ranges::find(entries, entry) == entries.end()) entries.push_back(std::move(entry));
    };
    for (std::size_t start = 0; start <= inherited.size();) {
        const std::size_t end = std::min(inherited.find(':', start), inherited.size());
        add(std::string(inherited.substr(start, end - start)));
        start = end + 1;
    }
    for (const NativePath& path : paths) {
        std::string entry = path.string();
        if (entry.find(':') != std::string::npos)
            return make_diag(ErrorDomain::Platform, kPathNotExposable)
                .arg("path", path)
                .kind(ErrorKind::InvalidInput)
                .fail();
        add(std::move(entry));
    }

    std::string joined;
    for (const std::string& entry : entries) {
        if (!joined.empty()) joined += ':';
        joined += entry;
    }
    return joined;
}

}  // namespace rb::os_linux::runner
