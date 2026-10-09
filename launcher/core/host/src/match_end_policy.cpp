#include "reboot/host/match_end_policy.hpp"

#include "reboot/host/host_error.hpp"

namespace rb::host {

Result<void> validate(const MatchEndPolicy& policy) {
    if (policy.delay < std::chrono::seconds::zero() || policy.delay > kMaxMatchEndDelay)
        return std::unexpected(to_diagnostic(HostError{.code = HostErrorCode::InvalidMatchEndDelay}));
    return {};
}

}  // namespace rb::host
