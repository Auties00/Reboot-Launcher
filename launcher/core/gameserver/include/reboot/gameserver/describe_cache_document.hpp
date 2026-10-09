#pragma once

#include <chrono>
#include <cstddef>
#include <string_view>
#include <vector>

#include <boost/json/object.hpp>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/gameserver/game_server_description.hpp"
#include "reboot/storage/load_report.hpp"

namespace rb::gameserver {

struct CachedDescription {
    Sha256Digest sha256{};
    std::chrono::system_clock::time_point described_at;
    GameServerDescription description;
};

// cache/game-server-describe.json, a storage::Document. Keyed by the binary's sha256, so a
// replaced binary is described again and a dev override does not evict the bundled entry.
struct DescribeCacheDocument {
    static constexpr std::string_view kName = "game-server-describe";
    static constexpr u32 kSchema = 1;
    static constexpr std::size_t kMaxEntries = 4;

    std::vector<CachedDescription> entries;
    boost::json::object unknown;

    [[nodiscard]] const CachedDescription* find(const Sha256Digest& sha256) const noexcept;
    // Replaces an entry with the same sha256; beyond kMaxEntries the oldest described_at goes.
    void put(CachedDescription entry);
    void erase(const Sha256Digest& sha256);

    [[nodiscard]] static DescribeCacheDocument read(const boost::json::object& values,
                                                    std::vector<storage::ValueIssue>& issues);
    [[nodiscard]] boost::json::object write() const;
    [[nodiscard]] static Result<boost::json::object> upgrade(boost::json::object values, u32 from_schema);
};

}  // namespace rb::gameserver
