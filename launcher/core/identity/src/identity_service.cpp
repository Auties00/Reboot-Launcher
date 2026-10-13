#include "reboot/identity/identity_service.hpp"

#include <algorithm>
#include <cstddef>
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
#include "reboot/storage/enum_names.hpp"

namespace rb::identity {

namespace {

[[nodiscard]] AccountRecord mint(AccountRole role, IRandom& random) {
    return AccountRecord{.record_id = AccountRecordId{uuid_v4(random)},
                         .role = role,
                         .display_name = default_display_name(role, random),
                         .tag = generate_tag(random),
                         .unknown = {}};
}

struct PickedRecords {
    IdentitySnapshot snapshot;
    // The document does not hold exactly these two records.
    bool rewrite = false;
};

// Fills a name or tag storage left empty from `known`, the record in memory, when it has the same record_id.
[[nodiscard]] bool repair(AccountRecord& record, const AccountRecord* known, IRandom& random) {
    const bool same = known != nullptr && known->record_id == record.record_id;
    bool repaired = false;
    if (!validate_display_name(record.display_name)) {
        const bool keep = same && validate_display_name(known->display_name);
        record.display_name = keep ? known->display_name : default_display_name(record.role, random);
        repaired = true;
    }
    if (record.tag.size() != kTagLength) {
        record.tag = same && known->tag.size() == kTagLength ? known->tag : generate_tag(random);
        repaired = true;
    }
    return repaired;
}

// The first record of each role, repaired. A missing one, or a host sharing the client's record_id, is the one in
// memory (`known`, absent at startup) when that one is still distinct, and minted otherwise.
[[nodiscard]] PickedRecords pick_records(const storage::AccountsDocument& document, const IdentitySnapshot* known,
                                         IRandom& random) {
    const auto find = [&document](AccountRole role, const AccountRecordId* other) -> const AccountRecord* {
        const auto it = std::ranges::find_if(document.records, [role, other](const AccountRecord& record) {
            return record.role == role && (other == nullptr || record.record_id != *other);
        });
        return it == document.records.end() ? nullptr : &*it;
    };
    PickedRecords picked{.snapshot = {}, .rewrite = document.records.size() != 2};
    const auto pick = [&](AccountRole role, const AccountRecordId* other) {
        const AccountRecord* in_memory = nullptr;
        if (known != nullptr) in_memory = role == AccountRole::Host ? &known->host : &known->client;
        if (const AccountRecord* stored = find(role, other)) {
            AccountRecord out = *stored;
            if (repair(out, in_memory, random)) picked.rewrite = true;
            return out;
        }
        picked.rewrite = true;
        if (in_memory != nullptr && (other == nullptr || in_memory->record_id != *other)) return *in_memory;
        return mint(role, random);
    };
    picked.snapshot.client = pick(AccountRole::Client, nullptr);
    picked.snapshot.host = pick(AccountRole::Host, &picked.snapshot.client.record_id);
    return picked;
}

[[nodiscard]] AccountRecord& slot(IdentitySnapshot& snapshot, AccountRole role) noexcept {
    return role == AccountRole::Host ? snapshot.host : snapshot.client;
}

void publish_change(EventBus& events, const AccountRecord& record) {
    const std::string_view role = storage::EnumNames<AccountRole>::kNames[static_cast<std::size_t>(record.role)];
    events.publish(EventKind::IdentityChanged, IdentityChangedEvent{record},
                   EventScope{.session = std::nullopt, .op = std::nullopt, .coalesce_key = std::string(role)});
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
        publish_change(events_, current_.client);
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
    static_cast<void>(accounts_.update([records = current_](storage::AccountsDocument& document) {
        document.records = {records.client, records.host};
    }));
}

void IdentityService::adopt(const storage::AccountsDocument& document) {
    PickedRecords picked = pick_records(document, &current_, random_);
    if (picked.rewrite)
        static_cast<void>(accounts_.update([records = picked.snapshot](storage::AccountsDocument& next) {
            next.records = {records.client, records.host};
        }));
    const IdentitySnapshot previous = std::exchange(current_, std::move(picked.snapshot));
    if (previous.client != current_.client) publish_change(events_, current_.client);
    if (previous.host != current_.host) publish_change(events_, current_.host);
}

Result<AccountRecord> IdentityService::set_display_name(AccountRole role, std::string_view name) {
    if (Result<void> valid = validate_display_name(name); !valid) return std::unexpected(std::move(valid.error()));
    if (record(role).display_name == name) return record(role);
    return rename(role, std::string(name));
}

Result<AccountRecord> IdentityService::reset(AccountRole role) {
    std::string name = default_display_name(role, random_);
    while (name == record(role).display_name) name = default_display_name(role, random_);
    return rename(role, std::move(name));
}

Result<AccountRecord> IdentityService::rename(AccountRole role, std::string name) {
    AccountRecord next = record(role);
    next.display_name = std::move(name);
    Result<u64> committed = accounts_.update([&next](storage::AccountsDocument& document) {
        const auto it = std::ranges::find(document.records, next.record_id, &AccountRecord::record_id);
        if (it != document.records.end()) *it = next;
        else document.records.push_back(next);
    });
    if (!committed) return std::unexpected(std::move(committed.error()));
    slot(current_, role) = next;
    publish_change(events_, next);
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
    publish_change(events_, current_.client);
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
