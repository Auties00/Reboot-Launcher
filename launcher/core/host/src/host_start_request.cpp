#include "reboot/host/host_start_request.hpp"

#include "reboot/host/host_error.hpp"

namespace reboot::host {

Result<void> check_start(const HostStartRequest& request, const HostProfile& profile) {
    const auto fail = [](HostErrorCode code) -> Result<void> {
        return std::unexpected(to_diagnostic(HostError{.code = code}));
    };
    if (request.linked_to && !profile.is_auto()) return fail(HostErrorCode::LinkedNeedsAutoProfile);
    if (!request.linked_to && profile.is_auto()) return fail(HostErrorCode::AutoProfileNeedsLink);
    if (profile.is_auto() && request.overrides.listing == HostListing::Listed)
        return fail(HostErrorCode::AutoProfileListed);
    if (request.overrides.port) return validate(PortPolicy{PinnedPorts{*request.overrides.port}});
    return {};
}

}  // namespace reboot::host
