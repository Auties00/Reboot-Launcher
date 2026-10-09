#include "reboot/host/host_profile.hpp"

#include <utility>

#include "reboot/host/host_error.hpp"
#include "reboot/publish/field_limits.hpp"

namespace rb::host {

namespace {

[[nodiscard]] std::unexpected<Diagnostic> fail(HostErrorCode code) {
    return std::unexpected(to_diagnostic(HostError{.code = code}));
}

}  // namespace

HostProfile new_profile(HostProfileId id, std::string name, HostListing default_listing) {
    HostProfile profile;
    profile.id = id;
    profile.name = std::move(name);
    profile.listing = default_listing;
    return profile;
}

HostProfile auto_profile() {
    HostProfile profile = new_profile(kAutoProfileId, "auto", HostListing::Unlisted);
    profile.port_mapping = false;
    return profile;
}

HostProfile reset_profile(HostProfile profile) {
    profile.listing = HostListing::Unlisted;
    if (profile.is_builtin()) {
        profile.server_name.clear();
        profile.description.clear();
        profile.match_end = MatchEndPolicy{};
    }
    return profile;
}

Result<HostProfile> validate(HostProfile profile) {
    if (profile.name.empty()) return fail(HostErrorCode::ProfileNameEmpty);
    if (profile.name.size() > kMaxProfileNameLength) return fail(HostErrorCode::ProfileNameTooLong);
    if (profile.build && profile.version) return fail(HostErrorCode::BuildAndVersion);
    if (profile.server_name.size() > publish::kMaxServerNameBytes) return fail(HostErrorCode::ServerNameTooLong);
    if (profile.description.size() > publish::kMaxDescriptionBytes) return fail(HostErrorCode::DescriptionTooLong);
    if (profile.is_auto() && profile.listing == HostListing::Listed) return fail(HostErrorCode::AutoProfileListed);
    if (auto port = validate(profile.port); !port) return std::unexpected(std::move(port.error()));
    if (auto match_end = validate(profile.match_end); !match_end)
        return std::unexpected(std::move(match_end.error()));
    auto operators = normalize(std::move(profile.operators));
    if (!operators) return std::unexpected(std::move(operators.error()));
    profile.operators = std::move(*operators);
    return profile;
}

}  // namespace rb::host
