#pragma once

#include <optional>
#include <vector>

#include <boost/json/value.hpp>

#include "reboot/browser/browse_choices.hpp"
#include "reboot/browser/join_target.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/net/mapping_record.hpp"

// The JSON forms of what the engine keeps in state.json for services that only hand it a value.
// Reading never fails: a malformed value reads as absent or default.
namespace rb::engine {

[[nodiscard]] boost::json::value join_target_to_json(const std::optional<browser::JoinTarget>& target);
[[nodiscard]] std::optional<browser::JoinTarget> join_target_from_json(const boost::json::value* value);

[[nodiscard]] boost::json::value browse_choices_to_json(const browser::BrowseChoices& choices);
[[nodiscard]] browser::BrowseChoices browse_choices_from_json(const boost::json::value* value);

[[nodiscard]] boost::json::value mapping_records_to_json(const std::vector<net::MappingRecord>& records);
[[nodiscard]] std::vector<net::MappingRecord> mapping_records_from_json(const boost::json::value* value);

struct SerialFloors {
    u64 catalog = 0;
    u64 manifest = 0;
};

[[nodiscard]] boost::json::value serials_to_json(const SerialFloors& floors);
[[nodiscard]] SerialFloors serials_from_json(const boost::json::value* value);

}  // namespace rb::engine
