#include <any>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <future>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "identity_file.hpp"
#include "messages.hpp"
#include "publish_test_support.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/publish/host_identity_store.hpp"
#include "reboot/publish/identity_hold.hpp"

using namespace reboot;
using namespace reboot::publish;
using namespace reboot::publish::test;

namespace {

[[nodiscard]] Diagnostic disk_error() { return make_diag(ErrorDomain::Publish, msg::kEdgeUnavailable).build(); }

[[nodiscard]] std::optional<StoredIdentity> stored(const StoreRig& rig, const HostProfileId& profile) {
    const auto bytes = rig.fs.contents(identity_file(profile));
    if (!bytes) return std::nullopt;
    return decode_identity(*bytes);
}

void write_identity(StoreRig& rig, const HostProfileId& profile, const ServerId& server,
                    std::optional<std::array<u8, kHostTokenSize>> token) {
    std::optional<HostToken> secret;
    if (token) secret.emplace(*token);
    const SecretBytes bytes = encode_identity(server, secret);
    rig.fs.write(identity_file(profile), bytes.reveal());
}

[[nodiscard]] ServerId server_of(u8 seed) {
    ServerId id;
    id.value.bytes.fill(seed);
    id.value.bytes[6] = 0x4A;
    return id;
}

[[nodiscard]] const Completed<std::any>* completed(const std::optional<ErasedOutcome>& outcome) {
    return outcome ? std::get_if<Completed<std::any>>(&*outcome) : nullptr;
}

[[nodiscard]] const Diagnostic* failure(const std::optional<ErasedOutcome>& outcome) {
    if (!outcome) return nullptr;
    const auto* failed = std::get_if<Failed>(&*outcome);
    return failed == nullptr ? nullptr : &failed->error;
}

// Keeps the rig's one worker busy until released; released at the latest on destruction.
class WorkerGate {
public:
    explicit WorkerGate(StoreRig& rig) {
        rig.workers.submit<bool>(
            [opened = open_.get_future().share()](CancelToken) -> Result<bool> {
                opened.wait();
                return true;
            },
            CancelToken{}, rig.strand, [](Result<bool>) {});
    }
    WorkerGate(const WorkerGate&) = delete;
    WorkerGate& operator=(const WorkerGate&) = delete;
    ~WorkerGate() { release(); }

    void release() {
        if (released_) return;
        released_ = true;
        open_.set_value();
    }

private:
    std::promise<void> open_;
    bool released_ = false;
};

}  // namespace

TEST_CASE("identity files round-trip and reject malformed records", "[publish][identity]") {
    const ServerId server = server_of(3);
    const SecretBytes with_token = encode_identity(server, HostToken(token_bytes(1)));
    const auto decoded = decode_identity(with_token.reveal());
    REQUIRE(decoded);
    CHECK(decoded->server == server);
    REQUIRE(decoded->token);
    CHECK(decoded->token->reveal() == token_bytes(1));

    const SecretBytes without = encode_identity(server, std::nullopt);
    const auto bare = decode_identity(without.reveal());
    REQUIRE(bare);
    CHECK_FALSE(bare->token);

    const auto text = [](std::string_view json) { return std::span<const u8>(reinterpret_cast<const u8*>(json.data()), json.size()); };
    CHECK_FALSE(decode_identity(text("not json")));
    CHECK_FALSE(decode_identity(text(R"({"token":"00"})")));
    CHECK_FALSE(decode_identity(text(R"({"server_id":"00000000-0000-0000-0000-000000000000"})")));
    CHECK_FALSE(decode_identity(text(R"({"server_id":"4a4a4a4a-4a4a-4a4a-4a4a-4a4a4a4a4a4a","token":"zz"})")));
    CHECK(decode_identity(text("\xEF\xBB\xBF{\"server_id\":\"4a4a4a4a-4a4a-4a4a-4a4a-4a4a4a4a4a4a\"}")));
}

TEST_CASE("ensure mints one id per profile and keeps it in an owner-only file", "[publish][identity]") {
    StoreRig rig;
    const HostProfileId profile = profile_id(1);
    REQUIRE(rig.store.load(std::span<const HostProfileId>(&profile, 1)));
    CHECK(rig.fs.owner_only(kIdentityDir));
    CHECK_FALSE(rig.store.server_id(profile));

    const auto first = rig.store.ensure(profile);
    REQUIRE(first);
    CHECK_FALSE(first->value.is_nil());
    CHECK((first->value.bytes[6] & 0xF0) == 0x40);
    const auto again = rig.store.ensure(profile);
    REQUIRE(again);
    CHECK(*again == *first);
    CHECK(rig.store.server_id(profile) == *first);

    REQUIRE(rig.settle());
    const auto on_disk = stored(rig, profile);
    REQUIRE(on_disk);
    CHECK(on_disk->server == *first);
    CHECK_FALSE(on_disk->token);
    CHECK(rig.fs.owner_only(identity_file(profile)));
}

TEST_CASE("load reads each profile's identity and masks its token", "[publish][identity]") {
    StoreRig rig;
    const std::array profiles{profile_id(1), profile_id(2), profile_id(3)};
    write_identity(rig, profiles[0], server_of(5), token_bytes(40));
    write_identity(rig, profiles[1], server_of(6), std::nullopt);

    const auto issues = rig.store.load(profiles);
    REQUIRE(issues);
    CHECK(issues->empty());
    CHECK(rig.store.server_id(profiles[0]) == server_of(5));
    CHECK(rig.store.server_id(profiles[1]) == server_of(6));
    CHECK_FALSE(rig.store.server_id(profiles[2]));

    const SecretString hex = token_text(HostToken(token_bytes(40)));
    CHECK(Logger::redactor().apply("token " + hex.reveal()).find(hex.reveal()) == std::string::npos);

    auto hold = rig.store.acquire(profiles[0]);
    REQUIRE(hold);
    REQUIRE(hold->identity().token);
    CHECK(hold->identity().token->reveal() == token_bytes(40));
}

TEST_CASE("load moves an unreadable identity aside and reports it", "[publish][identity]") {
    StoreRig rig;
    const HostProfileId profile = profile_id(4);
    rig.fs.write_text(identity_file(profile), "{ broken");

    const auto issues = rig.store.load(std::span<const HostProfileId>(&profile, 1));
    REQUIRE(issues);
    REQUIRE(issues->size() == 1);
    const IdentityLoadIssue& issue = issues->front();
    CHECK(issue.profile == profile);
    CHECK(issue.reason.is(msg::kIdentityUnreadable));
    REQUIRE(issue.quarantined_to);
    CHECK(rig.fs.text(*issue.quarantined_to) == "{ broken");
    CHECK(rig.fs.owner_only(*issue.quarantined_to));
    CHECK_FALSE(rig.fs.exists(identity_file(profile)));
    CHECK_FALSE(rig.store.server_id(profile));

    // The profile starts over with a fresh id on its next use.
    REQUIRE(rig.store.ensure(profile));
    REQUIRE(rig.settle());
    CHECK(stored(rig, profile));
}

TEST_CASE("load reports a file it cannot read without failing", "[publish][identity]") {
    StoreRig rig;
    const HostProfileId profile = profile_id(5);
    write_identity(rig, profile, server_of(1), std::nullopt);
    rig.fs.faults().fail_next(testing::FsOperation::ReadAll, disk_error());

    const auto issues = rig.store.load(std::span<const HostProfileId>(&profile, 1));
    REQUIRE(issues);
    REQUIRE(issues->size() == 1);
    CHECK_FALSE(issues->front().quarantined_to);
    CHECK(rig.fs.exists(identity_file(profile)));
}

TEST_CASE("load fails when the identity directory cannot be created", "[publish][identity]") {
    StoreRig rig;
    rig.fs.faults().fail_next(testing::FsOperation::CreateDirsOwnerOnly, disk_error());
    const auto loaded = rig.store.load({});
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().is(msg::kIdentityDirUnavailable));
}

TEST_CASE("only one hold per profile exists, released with the hold", "[publish][identity]") {
    StoreRig rig;
    REQUIRE(rig.store.load({}));
    const HostProfileId profile = profile_id(1);

    auto first = rig.store.acquire(profile);
    REQUIRE(first);
    CHECK(first->held());
    CHECK(first->identity().server == rig.store.server_id(profile));

    const auto second = rig.store.acquire(profile);
    REQUIRE_FALSE(second);
    CHECK(second.error().is(msg::kProfileBusy));

    IdentityHold moved = std::move(*first);
    CHECK_FALSE(first->held());
    CHECK(moved.held());
    CHECK_FALSE(rig.store.acquire(profile));

    moved = IdentityHold{};
    CHECK(rig.store.acquire(profile));
}

TEST_CASE("save_token writes the token and reports once it is on disk", "[publish][identity]") {
    StoreRig rig;
    REQUIRE(rig.store.load({}));
    const HostProfileId profile = profile_id(1);
    auto hold = rig.store.acquire(profile);
    REQUIRE(hold);

    std::optional<Result<void>> saved;
    rig.store.save_token(*hold, HostToken(token_bytes(9)), [&](Result<void> result) { saved = std::move(result); });
    REQUIRE(hold->identity().token);
    rig.strand.run_until([&] { return saved.has_value(); });
    CHECK(*saved);
    const auto on_disk = stored(rig, profile);
    REQUIRE(on_disk);
    REQUIRE(on_disk->token);
    CHECK(on_disk->token->reveal() == token_bytes(9));
}

TEST_CASE("a failed write keeps the token in memory and flush writes it again", "[publish][identity]") {
    StoreRig rig;
    REQUIRE(rig.store.load({}));
    const HostProfileId profile = profile_id(1);
    auto hold = rig.store.acquire(profile);
    REQUIRE(hold);
    REQUIRE(rig.settle());

    rig.fs.faults().fail_next(testing::FsOperation::AtomicReplace, disk_error());
    std::optional<Result<void>> saved;
    rig.store.save_token(*hold, HostToken(token_bytes(2)), [&](Result<void> result) { saved = std::move(result); });
    rig.strand.run_until([&] { return saved.has_value(); });
    REQUIRE_FALSE(*saved);
    CHECK(hold->identity().token);
    CHECK_FALSE(stored(rig, profile)->token);

    REQUIRE(rig.settle());
    const auto on_disk = stored(rig, profile);
    REQUIRE(on_disk->token);
    CHECK(on_disk->token->reveal() == token_bytes(2));
}

TEST_CASE("flush reports a write that keeps failing", "[publish][identity]") {
    StoreRig rig;
    REQUIRE(rig.store.load({}));
    rig.fs.faults().fail_always(testing::FsOperation::AtomicReplace, disk_error());
    REQUIRE(rig.store.ensure(profile_id(1)));
    const Result<void> flushed = rig.settle();
    REQUIRE_FALSE(flushed);
    CHECK(flushed.error().is(msg::kEdgeUnavailable));
}

TEST_CASE("rotate replaces the id and drops the token", "[publish][identity]") {
    StoreRig rig;
    const HostProfileId profile = profile_id(1);
    write_identity(rig, profile, server_of(7), token_bytes(3));
    REQUIRE(rig.store.load(std::span<const HostProfileId>(&profile, 1)));
    auto hold = rig.store.acquire(profile);
    REQUIRE(hold);

    std::optional<Result<void>> saved;
    const ServerId fresh = rig.store.rotate(*hold, [&](Result<void> result) { saved = std::move(result); });
    CHECK(fresh != server_of(7));
    CHECK(hold->identity().server == fresh);
    CHECK_FALSE(hold->identity().token);
    CHECK(rig.store.server_id(profile) == fresh);
    rig.strand.run_until([&] { return saved.has_value(); });
    CHECK(*saved);
    const auto on_disk = stored(rig, profile);
    CHECK(on_disk->server == fresh);
    CHECK_FALSE(on_disk->token);
}

TEST_CASE("remove refuses a held identity and deletes a free one", "[publish][identity]") {
    StoreRig rig;
    REQUIRE(rig.store.load({}));
    const HostProfileId profile = profile_id(1);
    {
        auto hold = rig.store.acquire(profile);
        REQUIRE(hold);
        const auto refused = rig.store.remove(profile, nullptr);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().is(msg::kIdentityInUse));
    }
    REQUIRE(rig.settle());
    REQUIRE(rig.fs.exists(identity_file(profile)));

    std::optional<Result<void>> removed;
    REQUIRE(rig.store.remove(profile, [&](Result<void> result) { removed = std::move(result); }));
    CHECK_FALSE(rig.store.server_id(profile));
    rig.strand.run_until([&] { return removed.has_value(); });
    CHECK(*removed);
    CHECK_FALSE(rig.fs.exists(identity_file(profile)));
}

TEST_CASE("export writes an owner-only copy into a new directory", "[publish][identity]") {
    StoreRig rig;
    REQUIRE(rig.store.load({}));
    const HostProfileId profile = profile_id(1);

    const auto unregistered = rig.store.start_export(profile, "/exports/a", DisconnectPolicy::Detached);
    REQUIRE_FALSE(unregistered);
    CHECK(unregistered.error().is(msg::kIdentityNotRegistered));

    {
        auto hold = rig.store.acquire(profile);
        REQUIRE(hold);
        rig.store.save_token(*hold, HostToken(token_bytes(5)), nullptr);
    }
    const auto started = rig.store.start_export(profile, "/exports/a", DisconnectPolicy::Detached);
    REQUIRE(started);
    const auto outcome = rig.wait_op(started->id());
    CHECK(completed(outcome) != nullptr);
    const NativePath file = NativePath("/exports/a") / kIdentityExportFile;
    CHECK(rig.fs.owner_only("/exports/a"));
    CHECK(rig.fs.owner_only(file));
    const auto exported = decode_identity(*rig.fs.contents(file));
    REQUIRE(exported);
    CHECK(exported->server == rig.store.server_id(profile));
    CHECK(exported->token->reveal() == token_bytes(5));

    const auto again = rig.store.start_export(profile, "/exports/a", DisconnectPolicy::Detached);
    REQUIRE(again);
    const auto again_outcome = rig.wait_op(again->id());
    const Diagnostic* exists = failure(again_outcome);
    REQUIRE(exists != nullptr);
    CHECK(exists->id == msg::kExportDestinationExists.id);
}

TEST_CASE("import replaces a free identity with an exported one", "[publish][identity]") {
    StoreRig rig;
    REQUIRE(rig.store.load({}));
    const HostProfileId profile = profile_id(2);
    const SecretBytes bytes = encode_identity(server_of(8), HostToken(token_bytes(6)));
    rig.fs.write("/moved/identity.json", bytes.reveal());

    const auto started = rig.store.start_import(profile, "/moved", DisconnectPolicy::Detached);
    REQUIRE(started);
    const auto outcome = rig.wait_op(started->id());
    const Completed<std::any>* done = completed(outcome);
    REQUIRE(done != nullptr);
    const ServerId* imported = std::any_cast<ServerId>(&done->value);
    REQUIRE(imported != nullptr);
    CHECK(*imported == server_of(8));
    CHECK(rig.store.server_id(profile) == server_of(8));
    const auto on_disk = stored(rig, profile);
    REQUIRE(on_disk);
    CHECK(on_disk->token->reveal() == token_bytes(6));
}

TEST_CASE("import refuses a held identity and a directory without a token", "[publish][identity]") {
    StoreRig rig;
    REQUIRE(rig.store.load({}));
    const HostProfileId profile = profile_id(3);
    {
        auto hold = rig.store.acquire(profile);
        REQUIRE(hold);
        const auto refused = rig.store.start_import(profile, "/moved", DisconnectPolicy::Detached);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().is(msg::kIdentityInUse));
    }

    const auto missing = rig.store.start_import(profile, "/nothing", DisconnectPolicy::Detached);
    REQUIRE(missing);
    const auto missing_outcome = rig.wait_op(missing->id());
    const Diagnostic* invalid = failure(missing_outcome);
    REQUIRE(invalid != nullptr);
    CHECK(invalid->id == msg::kIdentityFileInvalid.id);

    const SecretBytes bytes = encode_identity(server_of(9), std::nullopt);
    rig.fs.write("/tokenless/identity.json", bytes.reveal());
    const auto tokenless = rig.store.start_import(profile, "/tokenless", DisconnectPolicy::Detached);
    REQUIRE(tokenless);
    const auto tokenless_outcome = rig.wait_op(tokenless->id());
    const Diagnostic* no_token = failure(tokenless_outcome);
    REQUIRE(no_token != nullptr);
    CHECK(no_token->id == msg::kIdentityFileInvalid.id);
    CHECK(rig.store.server_id(profile) != server_of(9));
}

TEST_CASE("a cancelled import leaves the identity alone", "[publish][identity]") {
    StoreRig rig;
    REQUIRE(rig.store.load({}));
    const HostProfileId profile = profile_id(4);
    const ServerId before = *rig.store.ensure(profile);
    const SecretBytes bytes = encode_identity(server_of(10), HostToken(token_bytes(7)));
    rig.fs.write("/moved/identity.json", bytes.reveal());

    const auto started = rig.store.start_import(profile, "/moved", DisconnectPolicy::Detached);
    REQUIRE(started);
    REQUIRE(rig.ops.cancel(started->id(), CancelReason::User));
    const auto outcome = rig.wait_op(started->id());
    REQUIRE(outcome);
    CHECK(std::holds_alternative<Cancelled>(*outcome));
    rig.drain_workers();
    CHECK(rig.store.server_id(profile) == before);
}

TEST_CASE("an import whose file cannot be written restores the old identity", "[publish][identity]") {
    StoreRig rig;
    const HostProfileId profile = profile_id(5);
    write_identity(rig, profile, server_of(1), token_bytes(1));
    REQUIRE(rig.store.load(std::span<const HostProfileId>(&profile, 1)));
    const SecretBytes bytes = encode_identity(server_of(11), HostToken(token_bytes(8)));
    rig.fs.write("/moved/identity.json", bytes.reveal());

    rig.fs.faults().fail_next(testing::FsOperation::AtomicReplace, disk_error());
    const auto started = rig.store.start_import(profile, "/moved", DisconnectPolicy::Detached);
    REQUIRE(started);
    const auto outcome = rig.wait_op(started->id());
    const Diagnostic* error = failure(outcome);
    REQUIRE(error != nullptr);
    CHECK(error->is(msg::kEdgeUnavailable));
    CHECK(rig.store.server_id(profile) == server_of(1));

    REQUIRE(rig.settle());
    const auto on_disk = stored(rig, profile);
    REQUIRE(on_disk);
    CHECK(on_disk->server == server_of(1));
    auto hold = rig.store.acquire(profile);
    REQUIRE(hold);
    REQUIRE(hold->identity().token);
    CHECK(hold->identity().token->reveal() == token_bytes(1));
}

TEST_CASE("an import cancelled while its file is written restores the old identity", "[publish][identity]") {
    StoreRig rig;
    REQUIRE(rig.store.load({}));
    const HostProfileId profile = profile_id(6);
    const ServerId before = *rig.store.ensure(profile);
    REQUIRE(rig.settle());
    const SecretBytes bytes = encode_identity(server_of(12), HostToken(token_bytes(9)));
    rig.fs.write("/moved/identity.json", bytes.reveal());

    const auto started = rig.store.start_import(profile, "/moved", DisconnectPolicy::Detached);
    REQUIRE(started);
    // The import's file write queues behind the gate, so the cancel lands before it completes.
    WorkerGate gate(rig);
    rig.strand.run_until([&] { return rig.store.server_id(profile) == server_of(12); });
    // Nothing may publish the imported id before the import has finished.
    const auto busy = rig.store.acquire(profile);
    REQUIRE_FALSE(busy);
    CHECK(busy.error().is(msg::kProfileBusy));
    REQUIRE_FALSE(rig.store.remove(profile, nullptr));

    REQUIRE(rig.ops.cancel(started->id(), CancelReason::User));
    // A flush queued behind the import's write also covers the write that restores the old identity.
    std::optional<Result<void>> flushed;
    rig.store.flush([&](Result<void> result) { flushed = std::move(result); });
    gate.release();
    rig.strand.run_until([&] { return flushed.has_value(); });
    CHECK(*flushed);
    const auto outcome = rig.wait_op(started->id());
    REQUIRE(outcome);
    CHECK(std::holds_alternative<Cancelled>(*outcome));
    CHECK(rig.store.server_id(profile) == before);
    const auto on_disk = stored(rig, profile);
    REQUIRE(on_disk);
    CHECK(on_disk->server == before);
    CHECK_FALSE(on_disk->token);
    CHECK(rig.store.acquire(profile));
}

TEST_CASE("an import into a profile without an identity is undone when it fails", "[publish][identity]") {
    StoreRig rig;
    rig.store.load_memory_only();
    const HostProfileId profile = profile_id(7);
    const SecretBytes bytes = encode_identity(server_of(13), HostToken(token_bytes(10)));
    rig.fs.write("/moved/identity.json", bytes.reveal());

    const auto started = rig.store.start_import(profile, "/moved", DisconnectPolicy::Detached);
    REQUIRE(started);
    const auto outcome = rig.wait_op(started->id());
    const Diagnostic* error = failure(outcome);
    REQUIRE(error != nullptr);
    CHECK(error->is(msg::kIdentityDirUnavailable));
    CHECK_FALSE(rig.store.server_id(profile));
}

TEST_CASE("a memory-only store keeps identities but reports every save", "[publish][identity]") {
    StoreRig rig;
    rig.store.load_memory_only();
    const HostProfileId profile = profile_id(1);
    auto hold = rig.store.acquire(profile);
    REQUIRE(hold);

    std::optional<Result<void>> saved;
    rig.store.save_token(*hold, HostToken(token_bytes(1)), [&](Result<void> result) { saved = std::move(result); });
    rig.strand.run_until([&] { return saved.has_value(); });
    REQUIRE_FALSE(*saved);
    CHECK(saved->error().is(msg::kIdentityDirUnavailable));
    CHECK(hold->identity().token);
    CHECK(rig.settle());
    CHECK_FALSE(rig.fs.exists(identity_file(profile)));
}
