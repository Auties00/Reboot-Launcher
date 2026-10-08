#include "reboot/foundation/paths.hpp"

#include <concepts>
#include <system_error>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/ports/platform_paths.hpp"

namespace reboot {

namespace {

using NativeString = NativePath::string_type;

// UTF-16 on Windows, raw bytes on POSIX; templates keep the other platform's branch uninstantiated.
template <class String>
constexpr bool kWide = std::same_as<typename String::value_type, wchar_t>;

template <class String>
NativePath path_from_utf8(std::string_view text) {
    if constexpr (kWide<String>) {
        const std::u16string units = utf8_to_utf16(text);
        return NativePath(String(units.begin(), units.end()));
    } else {
        return NativePath(String(text));
    }
}

template <class String>
std::vector<u8> hashed_bytes(const String& native) {
    if constexpr (kWide<String>) {
        std::u16string units;
        units.reserve(native.size());
        for (const wchar_t c : native) units.push_back(static_cast<char16_t>(c));
        const std::string utf8 = utf16_to_utf8(units);
        return {utf8.begin(), utf8.end()};
    } else {
        return {native.begin(), native.end()};
    }
}

constexpr const char* kExeSuffix =
#if defined(_WIN32)
    ".exe";
#else
    "";
#endif

}  // namespace

Result<DataRoot> resolve_data_root(const ports::IPlatformPaths& paths, std::optional<std::string_view> env_override) {
    if (env_override && !env_override->empty()) {
        NativePath root = path_from_utf8<NativeString>(*env_override);
        if (!root.is_absolute())
            return make_diag(ErrorDomain::Foundation, msg::kDataRootNotAbsolute)
                .kind(ErrorKind::InvalidInput)
                .arg("path", root)
                .fail();
        return DataRoot{std::move(root), true};
    }
    NativePath root = paths.default_data_root();
    if (root.empty()) return make_diag(ErrorDomain::Foundation, msg::kNoDataRoot).fail();
    return DataRoot{std::move(root), false};
}

NativePath canonical_root(const DataRoot& root) {
    std::error_code error;
    NativePath absolute = std::filesystem::absolute(root.root, error);
    if (error) absolute = root.root;
    NativePath path = std::filesystem::weakly_canonical(absolute, error);
    if (error) path = absolute.lexically_normal();
    // "a/b/" and "a/b" name the same root and must hash alike.
    if (!path.has_filename() && path.has_relative_path()) path = path.parent_path();
    return path;
}

std::string root_hash16(const NativePath& canonical) {
    const std::vector<u8> bytes = hashed_bytes(canonical.native());
    return to_hex(sha256(bytes)).substr(0, 16);
}

AppLayout::AppLayout(const DataRoot& root, const ports::IPlatformPaths& paths)
    : root_(root.root),
      cache_(root.overridden ? root.root / "cache" : paths.default_cache_root()),
      logs_(root.overridden ? root.root / "logs" : paths.default_logs_root()) {}

InstallLayout locate_install(const ports::IPlatformPaths& paths, std::optional<NativePath> dev_override) {
    InstallLayout layout;
    layout.install_dir = dev_override ? std::move(*dev_override) : paths.exe_dir();
    layout.backend_exe = layout.install_dir / (std::string("reboot-backend") + kExeSuffix);
    layout.game_server_exe = layout.install_dir / (std::string("reboot-game-server") + kExeSuffix);
    layout.backend_content_dir = layout.install_dir / "backend-content";
    layout.bundled_catalog = layout.install_dir / "catalog.json";
    layout.bundled_manifest = layout.install_dir / "manifest.json";
    return layout;
}

bool is_inside(const NativePath& child, const NativePath& parent) {
    const NativePath normal_child = child.lexically_normal();
    const NativePath normal_parent = parent.lexically_normal();
    auto child_it = normal_child.begin();
    for (const NativePath& part : normal_parent) {
        // A trailing separator normalises to an empty last element, which matches anything.
        if (part.empty()) break;
        if (child_it == normal_child.end() || *child_it != part) return false;
        ++child_it;
    }
    return true;
}

}  // namespace reboot
