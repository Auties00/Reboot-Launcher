#pragma once

#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include <boost/json/value.hpp>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/storage/enum_names.hpp"

// One JSON form per shared type; *_from_json fails with storage.wrong_type or the id noted.
namespace reboot::storage {

[[nodiscard]] boost::json::value name_to_json(std::string_view name);
// storage.unknown_name.
[[nodiscard]] Result<std::size_t> name_index_from_json(const boost::json::value& value,
                                                       std::span<const std::string_view> names);

template <PersistedEnum E>
[[nodiscard]] boost::json::value enum_to_json(E value) {
    return name_to_json(EnumNames<E>::kNames[static_cast<std::size_t>(value)]);
}

template <PersistedEnum E>
[[nodiscard]] Result<E> enum_from_json(const boost::json::value& value) {
    return name_index_from_json(value, EnumNames<E>::kNames).transform([](std::size_t index) {
        return static_cast<E>(index);
    });
}

// A string, or {"native": base64} when not valid Unicode, so no bytes are lost. storage.invalid_path.
[[nodiscard]] boost::json::value path_to_json(const NativePath& path);
[[nodiscard]] Result<NativePath> path_from_json(const boost::json::value& value);

// Microseconds since the Unix epoch. storage.out_of_range.
[[nodiscard]] boost::json::value time_to_json(std::chrono::system_clock::time_point time);
[[nodiscard]] Result<std::chrono::system_clock::time_point> time_from_json(const boost::json::value& value);

// storage.invalid_uuid.
[[nodiscard]] boost::json::value uuid_to_json(const Uuid& uuid);
[[nodiscard]] Result<Uuid> uuid_from_json(const boost::json::value& value);

template <class Tag>
[[nodiscard]] boost::json::value id_to_json(const Id<Tag>& id) {
    return uuid_to_json(id.value);
}

template <class Tag>
[[nodiscard]] Result<Id<Tag>> id_from_json(const boost::json::value& value) {
    return uuid_from_json(value).transform([](const Uuid& uuid) { return Id<Tag>{uuid}; });
}

// storage.invalid_version.
[[nodiscard]] boost::json::value semver_to_json(const SemVer& version);
[[nodiscard]] Result<SemVer> semver_from_json(const boost::json::value& value);

// The canonical() text. storage.invalid_version.
[[nodiscard]] boost::json::value game_version_to_json(const GameVersion& version);
[[nodiscard]] Result<GameVersion> game_version_from_json(const boost::json::value& value);

// 1 to 65535. storage.out_of_range.
[[nodiscard]] boost::json::value port_to_json(Port port);
[[nodiscard]] Result<Port> port_from_json(const boost::json::value& value);

// storage.invalid_utf8.
[[nodiscard]] Result<std::string> string_from_json(const boost::json::value& value);
[[nodiscard]] Result<bool> bool_from_json(const boost::json::value& value);
// storage.out_of_range for a negative or fractional number.
[[nodiscard]] Result<u64> u64_from_json(const boost::json::value& value);

}  // namespace reboot::storage
