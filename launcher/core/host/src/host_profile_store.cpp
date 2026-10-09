#include "reboot/host/host_profile_store.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>
#include <utility>

#include "reboot/foundation/random.hpp"
#include "reboot/host/host_error.hpp"

namespace rb::host {

namespace {

constexpr std::string_view kDefaultProfileName = "default";

[[nodiscard]] bool same_name(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

[[nodiscard]] bool name_taken(const std::vector<HostProfile>& profiles, std::string_view name,
                              const HostProfileId& except) {
    return std::ranges::any_of(profiles, [&](const HostProfile& profile) {
        return profile.id != except && same_name(profile.name, name);
    });
}

// A built-in added back after a hand edit keeps its name unless a user profile took it.
[[nodiscard]] std::string free_name(const std::vector<HostProfile>& profiles, std::string_view base,
                                    const HostProfileId& id) {
    std::string name(base);
    for (u32 suffix = 2; name_taken(profiles, name, id); ++suffix) name = std::string(base) + "-" + std::to_string(suffix);
    return name;
}

[[nodiscard]] std::vector<HostProfile>::iterator find(std::vector<HostProfile>& profiles, const HostProfileId& id) {
    return std::ranges::find(profiles, id, &HostProfile::id);
}

[[nodiscard]] std::unexpected<Diagnostic> fail(HostError error) { return std::unexpected(to_diagnostic(error)); }

[[nodiscard]] bool reset_changes(const HostProfile& before, const HostProfile& after) {
    return before.listing != after.listing || before.server_name != after.server_name ||
           before.description != after.description || before.match_end != after.match_end;
}

}  // namespace

HostProfileStore::HostProfileStore(storage::DocumentStore<HostProfilesDocument>& store, IRandom& random)
    : store_(store), random_(random) {
    store_.set_on_reload([this](const HostProfilesDocument&, std::span<const storage::ValueIssue>) {
        if (on_reload_) on_reload_();
    });
}

HostProfileStore::~HostProfileStore() { store_.set_on_reload({}); }

bool HostProfileStore::add_builtins(std::vector<HostProfile>& profiles) const {
    if (!default_listing_) return false;
    std::vector<HostProfile> missing;
    if (find(profiles, kDefaultProfileId) == profiles.end()) {
        HostProfile profile = new_profile(kDefaultProfileId, std::string(kDefaultProfileName), *default_listing_);
        profile.name = free_name(profiles, profile.name, profile.id);
        profile.revision = 1;
        missing.push_back(std::move(profile));
    }
    if (find(profiles, kAutoProfileId) == profiles.end()) {
        HostProfile profile = auto_profile();
        profile.name = free_name(profiles, profile.name, profile.id);
        profile.revision = 1;
        missing.push_back(std::move(profile));
    }
    if (missing.empty()) return false;
    profiles.insert(profiles.begin(), std::make_move_iterator(missing.begin()), std::make_move_iterator(missing.end()));
    return true;
}

std::vector<HostProfile> HostProfileStore::merged() const {
    std::vector<HostProfile> profiles = store_.get().profiles;
    add_builtins(profiles);
    return profiles;
}

Result<void> HostProfileStore::commit(std::vector<HostProfile> profiles) {
    Result<u64> written = store_.update(
        [profiles = std::move(profiles)](HostProfilesDocument& document) mutable { document.profiles = std::move(profiles); });
    if (!written) return std::unexpected(std::move(written.error()));
    return {};
}

Result<void> HostProfileStore::ensure_builtin(HostListing default_listing) {
    default_listing_ = default_listing;
    std::vector<HostProfile> profiles = store_.get().profiles;
    if (!add_builtins(profiles)) return {};
    return commit(std::move(profiles));
}

std::vector<HostProfile> HostProfileStore::list() const { return merged(); }

Result<HostProfile> HostProfileStore::get(HostProfileId id) const {
    std::vector<HostProfile> profiles = merged();
    const auto found = find(profiles, id);
    if (found == profiles.end()) return fail({.code = HostErrorCode::ProfileNotFound, .profile = id});
    return std::move(*found);
}

Result<HostProfile> HostProfileStore::create(HostProfile draft) {
    std::vector<HostProfile> profiles = merged();
    do {
        draft.id = HostProfileId{uuid_v4(random_)};
    } while (draft.is_builtin() || find(profiles, draft.id) != profiles.end());
    draft.revision = 1;
    Result<HostProfile> profile = validate(std::move(draft));
    if (!profile) return profile;
    if (name_taken(profiles, profile->name, profile->id))
        return fail({.code = HostErrorCode::ProfileNameTaken, .name = profile->name});
    profiles.push_back(*profile);
    if (Result<void> written = commit(std::move(profiles)); !written) return std::unexpected(std::move(written.error()));
    return profile;
}

Result<HostProfile> HostProfileStore::update(HostProfile profile) {
    std::vector<HostProfile> profiles = merged();
    const auto stored = find(profiles, profile.id);
    if (stored == profiles.end()) return fail({.code = HostErrorCode::ProfileNotFound, .profile = profile.id});
    if (profile.revision != stored->revision)
        return fail({.code = HostErrorCode::ProfileStale, .profile = profile.id, .name = stored->name});
    Result<HostProfile> valid = validate(std::move(profile));
    if (!valid) return valid;
    if (name_taken(profiles, valid->name, valid->id))
        return fail({.code = HostErrorCode::ProfileNameTaken, .name = valid->name});
    valid->revision = stored->revision + 1;
    *stored = *valid;
    if (Result<void> written = commit(std::move(profiles)); !written) return std::unexpected(std::move(written.error()));
    return valid;
}

Result<void> HostProfileStore::remove(HostProfileId id) {
    std::vector<HostProfile> profiles = merged();
    const auto stored = find(profiles, id);
    if (stored == profiles.end()) return fail({.code = HostErrorCode::ProfileNotFound, .profile = id});
    if (stored->is_builtin()) return fail({.code = HostErrorCode::BuiltinProfile, .profile = id, .name = stored->name});
    profiles.erase(stored);
    return commit(std::move(profiles));
}

Result<void> HostProfileStore::reset() {
    std::vector<HostProfile> profiles = merged();
    for (HostProfile& profile : profiles) {
        HostProfile cleared = reset_profile(profile);
        if (!reset_changes(profile, cleared)) continue;
        cleared.revision = profile.revision + 1;
        profile = std::move(cleared);
    }
    return commit(std::move(profiles));
}

void HostProfileStore::set_on_reload(UniqueFunction<void()> on_reload) { on_reload_ = std::move(on_reload); }

}  // namespace rb::host
