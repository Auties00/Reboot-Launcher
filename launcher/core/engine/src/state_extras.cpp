#include "state_extras.hpp"

#include <utility>

namespace reboot::engine {

const boost::json::value* state_extra(const storage::StateDocument& state, std::string_view key) {
    return state.unknown.if_contains(key);
}

Result<void> put_state_extra(storage::DocumentStore<storage::StateDocument>& state, std::string_view key,
                             boost::json::value value) {
    Result<u64> written = state.update([key, value = std::move(value)](storage::StateDocument& document) mutable {
        if (value.is_null()) document.unknown.erase(key);
        else document.unknown[key] = std::move(value);
    });
    if (!written) return std::unexpected(std::move(written.error()));
    return {};
}

}  // namespace reboot::engine
