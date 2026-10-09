#include "secret_files.hpp"

#include <string>
#include <utility>
#include <vector>

#include "reboot/foundation/sha256.hpp"
#include "reboot/ports/file_system.hpp"

namespace reboot::os_macos::platform {

SecretFiles::SecretFiles(NativePath dir, ports::IFileSystem& fs) noexcept : dir_(std::move(dir)), fs_(fs) {}

NativePath SecretFiles::path_of(std::string_view key) const {
    const auto* bytes = reinterpret_cast<const u8*>(key.data());
    return dir_ / (to_hex(sha256(std::span<const u8>(bytes, key.size()))) + ".secret");
}

Result<void> SecretFiles::put(std::string_view key, std::span<const u8> value) {
    if (auto created = fs_.create_dirs_owner_only(dir_); !created) return created;
    const NativePath path = path_of(key);
    if (auto written = fs_.atomic_replace(path, value, false); !written) return written;
    return fs_.restrict_to_owner(path);
}

Result<std::optional<SecretBytes>> SecretFiles::get(std::string_view key) {
    Result<std::vector<u8>> bytes = fs_.read_all(path_of(key));
    if (!bytes) {
        if (bytes.error().kind == ErrorKind::NotFound) return std::optional<SecretBytes>{};
        return std::unexpected(std::move(bytes.error()));
    }
    return std::optional<SecretBytes>{SecretBytes{std::move(*bytes)}};
}

Result<void> SecretFiles::erase(std::string_view key) { return fs_.remove_tree(path_of(key)); }

}  // namespace reboot::os_macos::platform
