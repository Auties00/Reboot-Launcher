#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include <boost/json/object.hpp>
#include <boost/json/value.hpp>

#include "reboot/components/app_entry.hpp"
#include "reboot/components/component_ref.hpp"
#include "reboot/components/manifest_platform.hpp"
#include "reboot/components/remote_file.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

// The JSON names shared by the release manifest and the store index.
namespace reboot::components::json_fields {

template <class E>
struct Named {
    std::string_view name;
    E value;
};

inline constexpr std::array<Named<ManifestOs>, 3> kOsNames{{
    {"windows", ManifestOs::Windows},
    {"macos", ManifestOs::MacOs},
    {"linux", ManifestOs::Linux},
}};
inline constexpr std::array<Named<ManifestArch>, 2> kArchNames{{
    {"x64", ManifestArch::X64},
    {"arm64", ManifestArch::Arm64},
}};
inline constexpr std::array<Named<AppPackageKind>, 2> kAppKindNames{{
    {"velopack", AppPackageKind::Velopack},
    {"tarball", AppPackageKind::Tarball},
}};
inline constexpr std::array<Named<PayloadRole>, 2> kRoleNames{{
    {"client_dll", PayloadRole::ClientDll},
    {"winhost", PayloadRole::Winhost},
}};
inline constexpr std::array<Named<RuntimeKind>, 5> kRuntimeKindNames{{
    {"mac_wine", RuntimeKind::MacWine},
    {"umu", RuntimeKind::Umu},
    {"ge_proton", RuntimeKind::GeProton},
    {"kron_wine", RuntimeKind::KronWine},
    {"vc_redist", RuntimeKind::VcRedist},
}};

template <class E, std::size_t N>
[[nodiscard]] constexpr std::optional<E> find_named(const std::array<Named<E>, N>& names, std::string_view text) {
    for (const auto& [name, value] : names)
        if (name == text) return value;
    return std::nullopt;
}

template <class E, std::size_t N>
[[nodiscard]] constexpr std::string_view name_of(const std::array<Named<E>, N>& names, E value) {
    for (const auto& [name, enumerator] : names)
        if (enumerator == value) return name;
    return {};
}

[[nodiscard]] std::optional<Sha256Digest> parse_sha256(std::string_view hex);

// Field paths name the failing member, e.g. "payloads[2].files[0].sha256".
[[nodiscard]] std::string member_path(std::string_view parent, std::string_view key);
[[nodiscard]] std::string index_path(std::string_view parent, std::size_t index);

// Reads members of one object; every failure is components.manifest_malformed naming the member.
class Reader {
public:
    Reader(const boost::json::object& object, std::string path) : object_(&object), path_(std::move(path)) {}

    [[nodiscard]] static Result<Reader> of(const boost::json::value& value, std::string path);

    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] std::string at(std::string_view key) const { return member_path(path_, key); }
    // Absent and null are the same.
    [[nodiscard]] const boost::json::value* find(std::string_view key) const;

    [[nodiscard]] Result<const boost::json::value*> required(std::string_view key) const;
    [[nodiscard]] Result<u64> u64_at(std::string_view key) const;
    [[nodiscard]] Result<std::string> string_at(std::string_view key) const;
    // Present and empty fail alike.
    [[nodiscard]] Result<std::string> nonempty_string_at(std::string_view key) const;
    [[nodiscard]] Result<bool> optional_bool(std::string_view key) const;
    [[nodiscard]] Result<SemVer> semver_at(std::string_view key) const;
    [[nodiscard]] Result<std::optional<SemVer>> optional_semver(std::string_view key) const;
    [[nodiscard]] Result<std::chrono::system_clock::time_point> unix_ms_at(std::string_view key) const;
    // An absent array reads as empty.
    [[nodiscard]] Result<const boost::json::array*> optional_array(std::string_view key) const;

private:
    const boost::json::object* object_;
    std::string path_;
};

[[nodiscard]] Diagnostic malformed(std::string field);

// {"urls": [...], "sha256": "<hex>", "size": N}; no urls, no sha256 or a zero size is malformed.
[[nodiscard]] Result<RemoteFile> read_remote_file(const Reader& in);
void write_remote_file(boost::json::object& out, const RemoteFile& file);

// {"os": ..., "arch": ...}; nullopt for a name this build does not know.
[[nodiscard]] Result<std::optional<ManifestPlatform>> read_platform(const Reader& in);

}  // namespace reboot::components::json_fields
