#include "reboot/identity/identity_service.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <boost/json/object.hpp>

#include "backend_endpoint.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/identity/display_name.hpp"
#include "reboot/identity/identity_changed_event.hpp"

namespace rb::identity {

namespace {

[[nodiscard]] AccountRecord mint(AccountRole role, IRandom& random) {
    return AccountRecord{
        .record_id = AccountRecordId{uuid_v4(random)}, .role = role, .display_name = {}, .tag = generate_tag(random)};
}

struct PickedRecords {
    IdentitySnapshot snapshot;
    // The document does not hold exactly this name and these two records.
    bool rewrite = false;
};

// A stored name storage left empty is the one in memory (`known`, absent at startup), or a new default.
[[nodiscard]] std::string pick_name(const storage::AccountsDocument& document, const IdentitySnapshot* known,
                                    IRandom& random, bool& repaired) {
    if (validate_display_name(document.display_name)) return document.display_name;
    repaired = true;
    if (known != nullptr && validate_display_name(known->display_name())) return known->display_name();
    return default_display_name(random);
}

// The first record of each role, its tag repaired. A missing one, or a host sharing the client's record_id, is the
// one in memory (`known`) when that one is still distinct, and minted otherwise.
[[nodiscard]] PickedRecords pick_records(const storage::AccountsDocument& document, const IdentitySnapshot* known,
                                         IRandom& random) {
    const auto find = [&document](AccountRole role, const AccountRecordId* other) -> const storage::StoredAccount* {
        const auto it = std::ranges::find_if(document.records, [role, other](const storage::StoredAccount& record) {
            return record.role == role && (other == nullptr || record.record_id != *other);
        });
        return it == document.records.end() ? nullptr : &*it;
    };
    PickedRecords picked{.snapshot = {}, .rewrite = document.records.size() != 2};
    const std::string name = pick_name(document, known, random, picked.rewrite);
    const auto pick = [&](AccountRole role, const AccountRecordId* other) {
        const AccountRecord* in_memory = nullptr;
        if (known != nullptr) in_memory = role == AccountRole::Host ? &known->host : &known->client;
        AccountRecord out;
        if (const storage::StoredAccount* stored = find(role, other)) {
            out = AccountRecord{.record_id = stored->record_id, .role = role, .display_name = {}, .tag = stored->tag};
            if (out.tag.size() != kTagLength) {
                const bool same = in_memory != nullptr && in_memory->record_id == out.record_id;
                out.tag = same && in_memory->tag.size() == kTagLength ? in_memory->tag : generate_tag(random);
                picked.rewrite = true;
            }
        } else {
            picked.rewrite = true;
            const bool distinct = in_memory != nullptr && (other == nullptr || in_memory->record_id != *other);
            out = distinct ? *in_memory : mint(role, random);
        }
        out.display_name = name;
        return out;
    };
    picked.snapshot.client = pick(AccountRole::Client, nullptr);
    picked.snapshot.host = pick(AccountRole::Host, &picked.snapshot.client.record_id);
    // Both roles share the name, so only the tags keep their account ids apart.
    while (picked.snapshot.host.tag == picked.snapshot.client.tag) {
        picked.snapshot.host.tag = generate_tag(random);
        picked.rewrite = true;
    }
    return picked;
}

// Lays `identity` over `document`; a record already stored keeps its unknown members.
void store(storage::AccountsDocument& document, const IdentitySnapshot& identity) {
    std::vector<storage::StoredAccount> records;
    for (const AccountRecord* record : {&identity.client, &identity.host}) {
        const auto it = std::ranges::find(document.records, record->record_id, &storage::StoredAccount::record_id);
        records.push_back(storage::StoredAccount{.record_id = record->record_id,
                                                 .role = record->role,
                                                 .tag = record->tag,
                                                 .unknown = it != document.records.end() ? it->unknown
                                                                                        : boost::json::object{}});
    }
    document.display_name = identity.display_name();
    document.records = std::move(records);
}

void publish_change(EventBus& events, const IdentitySnapshot& identity) {
    events.publish(EventKind::IdentityChanged, IdentityChangedEvent{identity},
                   EventScope{.session = std::nullopt, .op = std::nullopt, .coalesce_key = "identity"});
}

}  // namespace

IdentityService::IdentityService(storage::DocumentStore<storage::AccountsDocument>& accounts,
                                 storage::DocumentStore<BackendLoginsDocument>& logins, IRandom& random,
                                 EventBus& events)
    : accounts_(accounts), logins_(logins), random_(random), events_(events) {
    accounts_.set_on_reload(
        [this](const storage::AccountsDocument& document, std::span<const storage::ValueIssue> issues) {
            for (const storage::ValueIssue& issue : issues)
                REBOOT_LOG_WARN(Storage, "accounts.json: {} was not usable: {}", issue.path, issue.reason.id);
            adopt(document);
        });
    // The edit may change the client's effective_login.
    logins_.set_on_reload([this](const BackendLoginsDocument&, std::span<const storage::ValueIssue>) {
        publish_change(events_, current_);
    });
}

IdentityService::~IdentityService() {
    accounts_.set_on_reload(nullptr);
    logins_.set_on_reload(nullptr);
}

void IdentityService::ensure_records() {
    PickedRecords picked = pick_records(accounts_.get(), nullptr, random_);
    current_ = std::move(picked.snapshot);
    if (!picked.rewrite) return;
    // A ReadOnly store refuses this; its LoadReport already said so and the records stay in memory.
    static_cast<void>(
        accounts_.update([identity = current_](storage::AccountsDocument& document) { store(document, identity); }));
}

void IdentityService::adopt(const storage::AccountsDocument& document) {
    PickedRecords picked = pick_records(document, &current_, random_);
    if (picked.rewrite)
        static_cast<void>(accounts_.update(
            [identity = picked.snapshot](storage::AccountsDocument& next) { store(next, identity); }));
    const IdentitySnapshot previous = std::exchange(current_, std::move(picked.snapshot));
    if (previous != current_) publish_change(events_, current_);
}

Result<IdentitySnapshot> IdentityService::set_display_name(std::string_view name) {
    if (Result<void> valid = validate_display_name(name); !valid) return std::unexpected(std::move(valid.error()));
    if (current_.display_name() == name) return current_;
    return rename(std::string(name));
}

Result<IdentitySnapshot> IdentityService::reset() {
    std::string name = default_display_name(random_);
    while (name == current_.display_name()) name = default_display_name(random_);
    return rename(std::move(name));
}

Result<IdentitySnapshot> IdentityService::rename(std::string name) {
    IdentitySnapshot next = current_;
    next.client.display_name = name;
    next.host.display_name = std::move(name);
    Result<u64> committed =
        accounts_.update([&next](storage::AccountsDocument& document) { store(document, next); });
    if (!committed) return std::unexpected(std::move(committed.error()));
    current_ = next;
    publish_change(events_, current_);
    return next;
}

BackendLogin IdentityService::backend_login(const HostPort& endpoint) const {
    const HostPort key = normalize_backend_endpoint(endpoint).value_or(endpoint);
    const std::vector<BackendLogin>& logins = logins_.get().logins;
    const auto it = std::ranges::find(logins, key, &BackendLogin::endpoint);
    return it != logins.end() ? *it : BackendLogin{.endpoint = key};
}

Result<BackendLogin> IdentityService::set_backend_login(BackendLogin login) {
    Result<HostPort> endpoint = normalize_backend_endpoint(login.endpoint);
    if (!endpoint) return std::unexpected(std::move(endpoint.error()));
    login.endpoint = std::move(*endpoint);
    if (login.login && login.login->empty()) return std::unexpected(empty_login(login.endpoint));
    const bool is_default = !login.login && login.policy == CredentialPolicy::Ticket;
    login.unknown = is_default ? boost::json::object{} : backend_login(login.endpoint).unknown;
    Result<u64> committed = logins_.update([&login, is_default](BackendLoginsDocument& document) {
        std::erase_if(document.logins, [&login](const BackendLogin& stored) { return stored.endpoint == login.endpoint; });
        if (!is_default) document.logins.push_back(login);
    });
    if (!committed) return std::unexpected(std::move(committed.error()));
    publish_change(events_, current_);
    return login;
}

LoginTarget IdentityService::login_target(const storage::BackendTarget& backend, UpstreamFlavor flavor,
                                          bool custom_auth_dll) const {
    LoginTarget target{.backend = backend.kind,
                       .flavor = flavor,
                       .remote_login = std::nullopt,
                       .policy = CredentialPolicy::Ticket,
                       .custom_auth_dll = custom_auth_dll};
    const HostPort* endpoint = nullptr;
    if (backend.kind == storage::BackendKind::Local) endpoint = &backend.local.endpoint;
    else if (backend.kind == storage::BackendKind::Remote && backend.remote) endpoint = &backend.remote->endpoint;
    if (endpoint == nullptr) return target;
    BackendLogin login = backend_login(*endpoint);
    target.remote_login = std::move(login.login);
    target.policy = login.policy;
    return target;
}

}  // namespace rb::identity
