#pragma once

#include <string_view>

#include <boost/json/value.hpp>

#include "reboot/foundation/diag.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/storage/state_document.hpp"

namespace reboot::engine {

// Engine bookkeeping that StateDocument has no member for lives under these keys of its unknown
// members, which every storage write keeps.
inline constexpr std::string_view kGuidanceKey = "guidance";
inline constexpr std::string_view kJoinTargetKey = "join_target";
inline constexpr std::string_view kBrowseChoicesKey = "browse_choices";
inline constexpr std::string_view kPortMappingsKey = "port_mappings";
inline constexpr std::string_view kSerialsKey = "serials";

// Null when the key is absent.
[[nodiscard]] const boost::json::value* state_extra(const storage::StateDocument& state, std::string_view key);

// A null `value` removes the key. Logs nothing: the caller decides what a failed write means.
Result<void> put_state_extra(storage::DocumentStore<storage::StateDocument>& state, std::string_view key,
                             boost::json::value value);

}  // namespace reboot::engine
