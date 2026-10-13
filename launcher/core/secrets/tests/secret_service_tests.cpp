#include <any>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/secrets/needs_secret.hpp"
#include "reboot/secrets/secret_error.hpp"
#include "reboot/secrets/secret_state_changed_event.hpp"
#include "reboot/testing/event_recorder.hpp"
#include "reboot/testing/expected_checks.hpp"
#include "service_fixture.hpp"

using namespace rb;
using namespace rb::secrets;
using namespace rb::secrets::test;
using rb::testing::SecretStoreOperation;
using rb::testing::held_value;

namespace {

constexpr SecretState kRemembered{SecretLocation::OsStore, false, false};
constexpr SecretState kSessionOnly{SecretLocation::Session, false, false};

RequestId ask_join_password(UserRequestRegistry& requests) {
    return requests.ask(UserRequestKind::NeedsJoinPassword, std::any{}, std::nullopt, std::nullopt,
                        [](const std::any&) -> Result<void> { return {}; }, CancelToken{});
}

std::optional<NeedsSecret> needs_secret(const UserRequestRegistry& requests, RequestId id) {
    for (const UserRequest& request : requests.pending())
        if (request.id == id && request.kind == UserRequestKind::NeedsSecret)
            if (const auto* payload = std::any_cast<NeedsSecret>(&request.payload)) return *payload;
    return std::nullopt;
}

// SecretService::require, with the raised request's id returned.
std::optional<RequestId> require(SecretService& service, const SecretTarget& target, SecretWait wait, CancelToken token,
                                 std::optional<Result<SecretBytes>>& done) {
    std::optional<RequestId> raised;
    service.require(target, wait, std::move(token), [&raised](RequestId id) { raised = id; },
                    [&done](Result<SecretBytes> result) { done = std::move(result); });
    return raised;
}

SecretTarget numbered_host_target(int i) {
    return SecretTarget::parse(SecretKind::HostJoinPassword, std::format("0a1b2c3d-4e5f-6071-8293-{:012}", i)).value();
}

// Holds the logger's writer inside write() while armed, and keeps what it was given.
struct LogGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool armed = false;
    bool entered = false;

    void arm() {
        const std::lock_guard lock(mutex);
        armed = true;
    }
    void wait_entered() {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return entered; });
    }
    void open() {
        {
            const std::lock_guard lock(mutex);
            armed = false;
        }
        changed.notify_all();
    }
};

class GateSink final : public LogSink {
public:
    GateSink(std::shared_ptr<LogGate> gate, std::shared_ptr<std::vector<std::string>> written)
        : gate_(std::move(gate)), written_(std::move(written)) {}

    void write(std::span<const LogRecord> records) override {
        std::unique_lock lock(gate_->mutex);
        for (const LogRecord& record : records) written_->push_back(record.text);
        if (!gate_->armed) return;
        gate_->entered = true;
        gate_->changed.notify_all();
        gate_->changed.wait(lock, [&] { return !gate_->armed; });
    }
    void flush() override {}

private:
    std::shared_ptr<LogGate> gate_;
    std::shared_ptr<std::vector<std::string>> written_;
};

// Notes whether a stored value was ever missing from the index chunks a load would read.
class ListingCheckStore final : public ports::ISecretStore {
public:
    explicit ListingCheckStore(rb::testing::FakeSecretStore& inner) : inner_(inner) {}

    [[nodiscard]] ports::SecretStoreKind kind() const override { return inner_.kind(); }
    Result<void> put(std::string_view key, std::span<const u8> value) override {
        Result<void> put = inner_.put(key, value);
        check();
        return put;
    }
    Result<std::optional<SecretBytes>> get(std::string_view key) override { return inner_.get(key); }
    Result<void> erase(std::string_view key) override {
        Result<void> erased = inner_.erase(key);
        check();
        return erased;
    }

    std::atomic<bool> unlisted{false};

private:
    void check() {
        std::set<std::string> listed;
        for (int chunk = 0;; ++chunk) {
            const std::string key = index_key() + (chunk == 0 ? "" : "." + std::to_string(chunk));
            Result<std::optional<SecretBytes>> index = inner_.get(key);
            if (!index || !*index) break;
            const std::string text = text_of(**index);
            for (std::size_t start = 0, end = 0; (end = text.find('\n', start)) != std::string::npos; start = end + 1)
                listed.insert(std::string(kRootHash) + "/" + text.substr(start, end - start));
        }
        for (const std::string& key : inner_.keys())
            if (!key.starts_with(index_key()) && !listed.contains(key)) unlisted = true;
    }

    rb::testing::FakeSecretStore& inner_;
};

}  // namespace

TEST_CASE("queries wait for start and an empty store loads as available", "[secrets][service]") {
    Fixture f;
    SecretService& service = f.make();
    const Result<SecretState> early = service.state(host_target());
    REQUIRE_FALSE(early.has_value());
    CHECK(has_error(early.error(), SecretError::NotReady));
    CHECK(has_error(service.provide(host_target()).error(), SecretError::NotReady));

    CHECK(f.start() == SecretsAvailability{ports::SecretStoreKind::Os, std::nullopt});
    CHECK(f.state(host_target()) == SecretState{});
    CHECK(has_error(service.provide(host_target()).error(), SecretError::NotFound));
}

TEST_CASE("a remembered secret is loaded by the next engine on the same data root", "[secrets][service]") {
    Fixture f;
    f.make();
    f.start();
    const Result<SecretState> saved = f.put_saved(host_target(), "hunter22");
    REQUIRE(saved.has_value());
    CHECK(*saved == kRemembered);
    REQUIRE(f.put_saved(remote_target(), "remote-password", Retention::Remember).has_value());
    CHECK(f.stored(value_key(host_target())) == "hunter22");

    f.make();
    f.start();
    CHECK(f.state(host_target()) == kRemembered);
    CHECK(f.state(remote_target()) == kRemembered);
    const Result<SecretBytes> revealed = f.service->reveal(host_target());
    REQUIRE(revealed.has_value());
    CHECK(text_of(*revealed) == "hunter22");
    CHECK(text_of(f.service->provide(remote_target()).value()) == "remote-password");

    f.make("ffeeddccbbaa9988");
    f.start();
    CHECK(f.state(host_target()) == SecretState{});
}

TEST_CASE("a store of kind File remembers into the file fallback", "[secrets][service]") {
    Fixture f(ports::SecretStoreKind::File);
    f.make();
    CHECK(f.start() == SecretsAvailability{ports::SecretStoreKind::File, std::nullopt});
    const Result<SecretState> saved = f.put_saved(host_target(), "hunter22");
    REQUIRE(saved.has_value());
    CHECK(saved->location == SecretLocation::FileStore);
}

TEST_CASE("without a store every secret is session-only", "[secrets][service]") {
    Fixture f(ports::SecretStoreKind::Unavailable);
    f.make();
    CHECK(f.start() == SecretsAvailability{ports::SecretStoreKind::Unavailable, SecretsUnavailableReason::NoOsStore});

    const Result<SecretState> saved = f.put_saved(host_target(), "hunter22");
    REQUIRE_FALSE(saved.has_value());
    CHECK(has_error(saved.error(), SecretError::StoreUnavailable));
    CHECK(f.state(host_target()) == SecretState{SecretLocation::Session, false, true});
    CHECK(text_of(f.service->provide(host_target()).value()) == "hunter22");

    const Result<SecretState> session = f.put_saved(remote_target(), "remote-password");
    REQUIRE(session.has_value());
    CHECK(*session == kSessionOnly);
    CHECK(f.clear(host_target()).has_value());
    CHECK(f.state(host_target()) == SecretState{});
}

TEST_CASE("a failed load leaves secrets session-only but clear still erases", "[secrets][service]") {
    Fixture f;
    f.make();
    f.start();
    REQUIRE(f.put_saved(host_target(), "hunter22").has_value());

    f.make();
    f.inner.faults().fail_next(SecretStoreOperation::Get, store_fault());
    const SecretsAvailability availability = f.start();
    CHECK(availability.unavailable == SecretsUnavailableReason::LoadFailed);
    CHECK(f.state(host_target()) == SecretState{});

    const Result<SecretState> saved = f.put_saved(remote_target(), "remote-password", Retention::Remember);
    REQUIRE_FALSE(saved.has_value());
    CHECK(has_error(saved.error(), SecretError::StoreReadFailed));
    CHECK(f.state(remote_target()) == SecretState{SecretLocation::Session, false, true});

    CHECK(f.clear(host_target()).has_value());
    CHECK_FALSE(f.stored(value_key(host_target())).has_value());
}

TEST_CASE("a load past the deadline is abandoned and its late result discarded", "[secrets][service]") {
    Fixture f;
    f.make();
    f.start();
    REQUIRE(f.put_saved(host_target(), "hunter22").has_value());

    f.make();
    f.store.close(SecretStoreOperation::Get);
    std::optional<SecretsAvailability> availability;
    f.service->start([&](SecretsAvailability result) { availability = result; });
    f.strand.advance(kStoreCallDeadline - std::chrono::milliseconds{1});
    CHECK_FALSE(availability.has_value());
    f.strand.advance(std::chrono::milliseconds{1});
    REQUIRE(availability.has_value());
    CHECK(availability->unavailable == SecretsUnavailableReason::LoadTimedOut);

    // The abandoned call still owns the store, so a later call cannot overtake it.
    const Result<void> wedged = f.clear(host_target());
    REQUIRE_FALSE(wedged.has_value());
    CHECK(has_error(wedged.error(), SecretError::StoreTimedOut));

    f.store.open();
    f.settle();
    CHECK(f.state(host_target()) == SecretState{});
    CHECK(f.clear(host_target()).has_value());
    CHECK_FALSE(f.stored(value_key(host_target())).has_value());
}

TEST_CASE("a put while loading is held at once and never replaced by the loaded copy", "[secrets][service]") {
    Fixture f;
    f.make();
    f.start();
    REQUIRE(f.put_saved(host_target(), "old-password").has_value());

    f.make();
    f.store.close(SecretStoreOperation::Get);
    std::optional<SecretsAvailability> availability;
    f.service->start([&](SecretsAvailability result) { availability = result; });
    std::optional<Result<SecretState>> saved;
    REQUIRE(f.service->put(host_target(), bytes("new-password"), std::nullopt,
                           [&](Result<SecretState> result) { saved = std::move(result); }));
    CHECK(text_of(f.service->provide(host_target()).value()) == "new-password");
    CHECK(has_error(f.service->state(host_target()).error(), SecretError::NotReady));

    f.store.open();
    f.strand.run_until([&] { return availability.has_value() && saved.has_value(); });
    REQUIRE(saved->has_value());
    CHECK(**saved == kRemembered);
    CHECK(text_of(f.service->reveal(host_target()).value()) == "new-password");
    CHECK(f.stored(value_key(host_target())) == "new-password");
}

TEST_CASE("a clear while loading discards the loaded copy and erases it", "[secrets][service]") {
    Fixture f;
    f.make();
    f.start();
    REQUIRE(f.put_saved(remote_target(), "remote-password", Retention::Remember).has_value());

    f.make();
    f.store.close(SecretStoreOperation::Get);
    std::optional<SecretsAvailability> availability;
    f.service->start([&](SecretsAvailability result) { availability = result; });
    std::optional<Result<void>> cleared;
    f.service->clear(remote_target(), [&](Result<void> result) { cleared = std::move(result); });
    f.store.open();
    f.strand.run_until([&] { return availability.has_value() && cleared.has_value(); });
    CHECK(cleared->has_value());
    CHECK(f.state(remote_target()) == SecretState{});
    CHECK_FALSE(f.stored(value_key(remote_target())).has_value());
    CHECK_FALSE(f.stored(index_key()).has_value());
}

TEST_CASE("a refused write keeps the value for this run only", "[secrets][service]") {
    Fixture f;
    f.make();
    f.start();
    f.inner.faults().fail_next(SecretStoreOperation::Put, store_fault());
    const Result<SecretState> saved = f.put_saved(host_target(), "hunter22");
    REQUIRE_FALSE(saved.has_value());
    CHECK(has_error(saved.error(), SecretError::StoreWriteFailed));
    REQUIRE(saved.error().causes.size() == 1);
    CHECK(saved.error().causes[0].id == "platform.secret_store_failed");
    CHECK(f.state(host_target()) == SecretState{SecretLocation::Session, false, true});
    CHECK(text_of(f.service->provide(host_target()).value()) == "hunter22");

    REQUIRE(f.put_saved(host_target(), "hunter22").has_value());
    CHECK(f.state(host_target()) == kRemembered);

    f.make();
    f.start();
    CHECK(f.state(host_target()) == kRemembered);
}

TEST_CASE("a write past the deadline fails, and later calls wait for the store to answer", "[secrets][service]") {
    Fixture f;
    f.make();
    f.start();
    f.store.close(SecretStoreOperation::Put);
    std::optional<Result<SecretState>> first;
    REQUIRE(f.service->put(host_target(), bytes("first-password"), std::nullopt,
                           [&](Result<SecretState> result) { first = std::move(result); }));
    CHECK(f.state(host_target()) == SecretState{SecretLocation::Session, true, false});
    f.strand.advance(kStoreCallDeadline);
    REQUIRE(first.has_value());
    REQUIRE_FALSE(first->has_value());
    CHECK(has_error(first->error(), SecretError::StoreTimedOut));
    CHECK(f.state(host_target()) == SecretState{SecretLocation::Session, false, true});

    const Result<SecretState> second = f.put_saved(host_target(), "second-password");
    REQUIRE_FALSE(second.has_value());
    CHECK(has_error(second.error(), SecretError::StoreTimedOut));

    // The late reply of the first write changes nothing.
    f.store.open();
    f.settle();
    CHECK(f.state(host_target()) == SecretState{SecretLocation::Session, false, true});

    REQUIRE(f.put_saved(host_target(), "third-password").has_value());
    CHECK(f.state(host_target()) == kRemembered);
    CHECK(f.stored(value_key(host_target())) == "third-password");
}

TEST_CASE("a clear erases what a write abandoned at its deadline stored late", "[secrets][service]") {
    Fixture f;
    f.make();
    f.start();
    f.store.close(SecretStoreOperation::Put);
    std::optional<Result<SecretState>> saved;
    REQUIRE(f.service->put(remote_target(), bytes("remote-password"), Retention::Remember,
                           [&](Result<SecretState> result) { saved = std::move(result); }));
    f.strand.advance(kStoreCallDeadline);
    REQUIRE(saved.has_value());
    CHECK(has_error(saved->error(), SecretError::StoreTimedOut));
    f.store.open();
    f.settle();
    REQUIRE(f.stored(value_key(remote_target())) == "remote-password");

    CHECK(f.clear(remote_target()).has_value());
    CHECK(f.inner.keys().empty());
    f.make();
    f.start();
    CHECK(f.state(remote_target()) == SecretState{});
}

TEST_CASE("a write after an erase abandoned at its deadline lists its target again", "[secrets][service]") {
    Fixture f;
    f.make();
    f.start();
    REQUIRE(f.put_saved(host_target(), "old-password").has_value());
    f.store.close(SecretStoreOperation::Erase);
    std::optional<Result<void>> cleared;
    f.service->clear(host_target(), [&](Result<void> result) { cleared = std::move(result); });
    f.strand.advance(kStoreCallDeadline);
    REQUIRE(cleared.has_value());
    CHECK(has_error(cleared->error(), SecretError::StoreTimedOut));
    f.store.open();
    f.settle();
    REQUIRE_FALSE(f.stored(index_key()).has_value());

    REQUIRE(f.put_saved(host_target(), "new-password").has_value());
    f.make();
    f.start();
    CHECK(f.state(host_target()) == kRemembered);
    CHECK(text_of(f.service->reveal(host_target()).value()) == "new-password");
}

TEST_CASE("writes for one target apply in call order", "[secrets][service]") {
    Fixture f;
    f.make();
    f.start();
    f.store.close(SecretStoreOperation::Put);
    std::optional<Result<SecretState>> first;
    std::optional<Result<SecretState>> second;
    REQUIRE(f.service->put(host_target(), bytes("first-password"), std::nullopt,
                           [&](Result<SecretState> result) { first = std::move(result); }));
    REQUIRE(f.service->put(host_target(), bytes("second-password"), std::nullopt,
                           [&](Result<SecretState> result) { second = std::move(result); }));
    f.store.open();
    f.strand.run_until([&] { return first.has_value() && second.has_value(); });
    CHECK(first->has_value());
    REQUIRE(second->has_value());
    CHECK(**second == kRemembered);
    CHECK(f.stored(value_key(host_target())) == "second-password");
}

TEST_CASE("a session put erases the stored remote password", "[secrets][service]") {
    Fixture f;
    f.make();
    f.start();
    REQUIRE(f.put_saved(remote_target(), "remote-password", Retention::Remember).has_value());
    REQUIRE(f.put_saved(host_target(), "hunter22").has_value());

    const Result<SecretState> session = f.put_saved(remote_target(), "other-password", Retention::Session);
    REQUIRE(session.has_value());
    CHECK(*session == kSessionOnly);
    CHECK_FALSE(f.stored(value_key(remote_target())).has_value());
    CHECK(f.stored(index_key()) == "host-join-password/0a1b2c3d-4e5f-6071-8293-a4b5c6d7e8f9\n");

    f.make();
    f.start();
    CHECK(f.state(remote_target()) == SecretState{});
    CHECK(f.state(host_target()) == kRemembered);
}

TEST_CASE("clear erases the stored copy and reports a failed erase", "[secrets][service]") {
    Fixture f;
    f.make();
    f.start();
    REQUIRE(f.put_saved(host_target(), "hunter22").has_value());
    f.inner.faults().fail_next(SecretStoreOperation::Erase, store_fault());
    const Result<void> failed = f.clear(host_target());
    REQUIRE_FALSE(failed.has_value());
    CHECK(has_error(failed.error(), SecretError::StoreEraseFailed));
    CHECK(f.state(host_target()) == SecretState{});

    CHECK(f.clear(host_target()).has_value());
    CHECK(f.inner.keys().empty());
}

TEST_CASE("every state change publishes a SecretStateChanged", "[secrets][service]") {
    Fixture f;
    rb::testing::EventRecorder recorder(f.events, EventFilter{.kinds = {EventKind::SecretStateChanged}});
    f.make();
    f.start();
    std::optional<Result<SecretState>> saved;
    REQUIRE(f.service->put(host_target(), bytes("hunter22"), std::nullopt,
                           [&](Result<SecretState> result) { saved = std::move(result); }));
    recorder.pump();
    f.strand.run_until([&] { return saved.has_value(); });
    recorder.pump();
    REQUIRE(f.clear(host_target()).has_value());
    recorder.pump();

    const auto changes = recorder.payloads<SecretStateChangedEvent>(EventKind::SecretStateChanged);
    REQUIRE(changes.size() == 3);
    CHECK(changes[0]->state == SecretState{SecretLocation::Session, true, false});
    CHECK(changes[1]->state == kRemembered);
    CHECK(changes[2]->state == SecretState{});
    for (const SecretStateChangedEvent* change : changes) CHECK(change->target == host_target());
    CHECK(recorder.events()[0].coalesce_key == "host-join-password/0a1b2c3d-4e5f-6071-8293-a4b5c6d7e8f9");
}

TEST_CASE("a put that breaks the rules is refused and changes nothing", "[secrets][service]") {
    Fixture f;
    SecretService& service = f.make();
    f.start();
    const auto refused = [&](const SecretTarget& target, std::string value, std::optional<Retention> retention) {
        const Result<void> result = service.put(target, bytes(value), retention, nullptr);
        REQUIRE_FALSE(result.has_value());
        CHECK(exit_code_for(result.error()) != 0);
        return result.error();
    };
    CHECK(has_error(refused(host_target(), "", std::nullopt), SecretError::EmptyValue));
    CHECK(has_error(refused(host_target(), std::string(kMaxSecretBytes + 1, 'x'), std::nullopt), SecretError::TooLarge));
    CHECK(has_error(refused(host_target(), "hunter22", Retention::Session), SecretError::RetentionNotAllowed));
    const RequestId request = ask_join_password(f.requests);
    CHECK(has_error(refused(join_target(request), "join-password", Retention::Remember),
                    SecretError::RetentionNotAllowed));
    CHECK(has_error(refused(join_target(RequestId{request.value + 1}), "join-password", std::nullopt),
                    SecretError::RequestNotPending));
    CHECK(service.put(host_target(), bytes(std::string(kMaxSecretBytes, 'x')), std::nullopt, nullptr).has_value());
    f.strand.run_until([&] { return f.state(host_target()) == kRemembered; });
    CHECK(f.state(join_target(request)) == SecretState{});
}

TEST_CASE("only the host join password can be read back by a client", "[secrets][service]") {
    Fixture f;
    SecretService& service = f.make();
    f.start();
    REQUIRE(f.put_saved(remote_target(), "remote-password").has_value());
    CHECK(has_error(service.reveal(remote_target()).error(), SecretError::RevealForbidden));
    CHECK(has_error(service.take(remote_target()).error(), SecretError::RevealForbidden));
    CHECK(text_of(service.provide(remote_target()).value()) == "remote-password");
}

TEST_CASE("a join password is taken once and dropped when its request resolves", "[secrets][service]") {
    Fixture f;
    SecretService& service = f.make();
    f.start();
    const RequestId first = ask_join_password(f.requests);
    REQUIRE(held_value(f.put_saved(join_target(first), "join-password")) == kSessionOnly);
    CHECK(has_error(service.reveal(join_target(first)).error(), SecretError::RevealForbidden));
    const Result<SecretBytes> taken = service.take(join_target(first));
    REQUIRE(taken.has_value());
    CHECK(text_of(*taken) == "join-password");
    CHECK(f.state(join_target(first)) == SecretState{});
    CHECK(has_error(service.take(join_target(first)).error(), SecretError::NotFound));

    const RequestId second = ask_join_password(f.requests);
    REQUIRE(f.put_saved(join_target(second), "never-taken").has_value());
    REQUIRE(f.requests.respond(second, std::any{}).has_value());
    f.strand.run_ready();
    CHECK(f.state(join_target(second)) == SecretState{});
    CHECK(has_error(service.put(join_target(second), bytes("too-late"), std::nullopt, nullptr).error(),
                    SecretError::RequestNotPending));
    CHECK(f.inner.keys().empty());
}

TEST_CASE("require hands over a held secret without asking", "[secrets][service]") {
    Fixture f;
    SecretService& service = f.make();
    f.start();
    REQUIRE(f.put_saved(remote_target(), "remote-password").has_value());
    std::optional<Result<SecretBytes>> done;
    const std::optional<RequestId> raised = require(service, remote_target(), SecretWait{}, CancelToken{}, done);
    CHECK_FALSE(raised.has_value());
    CHECK_FALSE(done.has_value());
    f.strand.run_ready();
    REQUIRE(done.has_value());
    CHECK(text_of(done->value()) == "remote-password");
    CHECK(f.requests.pending().empty());
}

TEST_CASE("require waits for the load instead of asking for a stored secret", "[secrets][service]") {
    Fixture f;
    f.make();
    f.start();
    REQUIRE(f.put_saved(remote_target(), "stored-password", Retention::Remember).has_value());

    SecretService& service = f.make();
    f.store.close(SecretStoreOperation::Get);
    std::optional<SecretsAvailability> availability;
    service.start([&](SecretsAvailability result) { availability = result; });
    std::optional<Result<SecretBytes>> done;
    CHECK_FALSE(require(service, remote_target(), SecretWait{}, CancelToken{}, done).has_value());
    CHECK(f.requests.pending().empty());
    // A rejected value is asked for again at once; the stored copy cannot answer it.
    std::optional<Result<SecretBytes>> rejected;
    CHECK(require(service, host_target(), SecretWait{std::nullopt, std::nullopt, NeedsSecretReason::Rejected},
                  CancelToken{}, rejected)
              .has_value());

    f.store.open();
    f.strand.run_until([&] { return done.has_value(); });
    REQUIRE(availability.has_value());
    REQUIRE(done->has_value());
    CHECK(text_of(**done) == "stored-password");
    CHECK(f.requests.pending().size() == 1);
    CHECK_FALSE(rejected.has_value());
}

TEST_CASE("require starts the load and asks once it found nothing stored", "[secrets][service]") {
    Fixture f;
    SecretService& service = f.make();
    std::optional<Result<SecretBytes>> done;
    std::optional<RequestId> raised;
    service.require(remote_target(), SecretWait{}, CancelToken{}, [&](RequestId id) { raised = id; },
                    [&](Result<SecretBytes> result) { done = std::move(result); });
    CHECK_FALSE(raised.has_value());
    f.strand.run_until([&] { return raised.has_value(); });
    CHECK(needs_secret(f.requests, *raised).has_value());
    CHECK(f.service->availability().available());

    CancelSource cancel;
    f.make();
    f.store.close(SecretStoreOperation::Get);
    const std::size_t pending = f.requests.pending().size();
    std::optional<Result<SecretBytes>> withdrawn;
    CHECK_FALSE(require(*f.service, remote_target(), SecretWait{}, cancel.token(), withdrawn).has_value());
    // Withdrawn at once, while the load is still held in the store.
    cancel.cancel(CancelReason::User);
    f.strand.run_until([&] { return withdrawn.has_value(); });
    CHECK(has_error(withdrawn->error(), SecretError::RequestWithdrawn));
    f.store.open();
    f.start();
    CHECK(f.requests.pending().size() == pending);

    // One cancelled before the call is withdrawn the same way.
    CancelSource already;
    already.cancel(CancelReason::Shutdown);
    f.make();
    f.store.close(SecretStoreOperation::Get);
    std::optional<Result<SecretBytes>> late;
    CHECK_FALSE(require(*f.service, remote_target(), SecretWait{}, already.token(), late).has_value());
    f.strand.run_until([&] { return late.has_value(); });
    CHECK(has_error(late->error(), SecretError::RequestWithdrawn));
    f.store.open();
    f.start();
    CHECK(f.requests.pending().size() == pending);
}

TEST_CASE("NeedsSecret is answered only after a fresh secret was put", "[secrets][service]") {
    Fixture f;
    SecretService& service = f.make();
    f.start();
    const SessionId session{Uuid{}};
    std::optional<Result<SecretBytes>> done;
    const std::optional<RequestId> raised =
        require(service, remote_target(), SecretWait{session, OpId{7}, NeedsSecretReason::Missing}, CancelToken{}, done);
    REQUIRE(raised.has_value());
    const std::optional<NeedsSecret> payload = needs_secret(f.requests, *raised);
    REQUIRE(payload.has_value());
    CHECK(payload->target == remote_target());
    CHECK(payload->reason == NeedsSecretReason::Missing);
    CHECK(payload->session == session);
    CHECK(payload->remember_location == SecretLocation::OsStore);
    CHECK(f.requests.pending()[0].op == OpId{7});

    const Result<void> early = f.requests.respond(*raised, SecretProvided{});
    REQUIRE_FALSE(early.has_value());
    CHECK(has_error(early.error(), SecretError::AnswerWithoutSecret));
    const Result<void> wrong = f.requests.respond(*raised, std::string("remote-password"));
    REQUIRE_FALSE(wrong.has_value());
    CHECK(has_error(wrong.error(), SecretError::AnswerWithoutSecret));

    REQUIRE(f.put_saved(remote_target(), "remote-password").has_value());
    REQUIRE(f.requests.respond(*raised, SecretProvided{}).has_value());
    CHECK_FALSE(done.has_value());
    f.strand.run_ready();
    REQUIRE(done.has_value());
    CHECK(text_of(done->value()) == "remote-password");
}

TEST_CASE("a rejected secret is asked for again even while held", "[secrets][service]") {
    Fixture f;
    SecretService& service = f.make();
    f.start();
    REQUIRE(f.put_saved(host_target(), "wrong-password").has_value());
    std::optional<Result<SecretBytes>> done;
    const std::optional<RequestId> raised = require(
        service, host_target(), SecretWait{std::nullopt, std::nullopt, NeedsSecretReason::Rejected}, CancelToken{}, done);
    REQUIRE(raised.has_value());
    const std::optional<NeedsSecret> payload = needs_secret(f.requests, *raised);
    REQUIRE(payload.has_value());
    CHECK(payload->reason == NeedsSecretReason::Rejected);
    CHECK(has_error(f.requests.respond(*raised, SecretProvided{}).error(), SecretError::AnswerWithoutSecret));

    REQUIRE(f.put_saved(host_target(), "right-password").has_value());
    REQUIRE(f.requests.respond(*raised, SecretProvided{}).has_value());
    f.strand.run_ready();
    REQUIRE(done.has_value());
    CHECK(text_of(done->value()) == "right-password");
}

TEST_CASE("cancelling a wait withdraws its request", "[secrets][service]") {
    Fixture f;
    SecretService& service = f.make();
    f.start();
    CancelSource cancel;
    std::optional<Result<SecretBytes>> done;
    const std::optional<RequestId> raised = require(service, remote_target(), SecretWait{}, cancel.token(), done);
    REQUIRE(raised.has_value());
    cancel.cancel(CancelReason::User);
    CHECK(f.requests.pending().empty());
    f.strand.run_ready();
    REQUIRE(done.has_value());
    REQUIRE_FALSE(done->has_value());
    CHECK(has_error(done->error(), SecretError::RequestWithdrawn));
    CHECK(f.requests.respond(*raised, SecretProvided{}).error().id == "requests.already_resolved");

    CancelSource already;
    already.cancel(CancelReason::Shutdown);
    std::optional<Result<SecretBytes>> late;
    REQUIRE(require(service, remote_target(), SecretWait{}, already.token(), late));
    f.strand.run_ready();
    REQUIRE(late.has_value());
    CHECK(has_error(late->error(), SecretError::RequestWithdrawn));
}

TEST_CASE("resolutions lost to a Resync are recovered from the pending requests", "[secrets][service]") {
    Fixture f;
    SecretService& service = f.make();
    f.start();
    CancelSource cancel;
    std::optional<Result<SecretBytes>> done;
    REQUIRE(require(service, remote_target(), SecretWait{}, cancel.token(), done));
    const RequestId join = ask_join_password(f.requests);
    REQUIRE(f.put_saved(join_target(join), "join-password").has_value());

    // Resolutions the strand has not drained yet overflow the service's queue.
    cancel.cancel(CancelReason::User);
    REQUIRE(f.requests.respond(join, std::any{}).has_value());
    for (int i = 0; i < 5000; ++i) {
        const RequestId flood = f.requests.ask(UserRequestKind::ConfirmJoin, std::any{}, std::nullopt, std::nullopt,
                                               [](const std::any&) -> Result<void> { return {}; }, CancelToken{});
        REQUIRE(f.requests.respond(flood, std::any{}).has_value());
    }
    f.strand.run_ready();
    REQUIRE(done.has_value());
    CHECK(has_error(done->error(), SecretError::RequestWithdrawn));
    CHECK(f.state(join_target(join)) == SecretState{});
}

TEST_CASE("held values are masked until dropped", "[secrets][service]") {
    Fixture f;
    f.make();
    f.start();
    const auto masked = [&](std::string_view value) {
        return f.redactor.apply("before " + std::string(value) + " after").find(value) == std::string::npos;
    };
    REQUIRE(f.put_saved(remote_target(), "first-secret").has_value());
    CHECK(masked("first-secret"));
    // The put itself drops the old value, before any store call.
    REQUIRE(f.service->put(remote_target(), bytes("second-secret"), std::nullopt, nullptr).has_value());
    CHECK_FALSE(masked("first-secret"));
    CHECK(masked("second-secret"));
    f.settle();

    REQUIRE(f.clear(remote_target()).has_value());
    CHECK_FALSE(masked("second-secret"));

    REQUIRE(f.put_saved(host_target(), "hunter22").has_value());
    f.make();
    CHECK_FALSE(masked("hunter22"));
    f.start();
    CHECK(masked("hunter22"));
}

TEST_CASE("a service destroyed during a store call ignores the late reply", "[secrets][service]") {
    Fixture f;
    f.make();
    f.start();
    f.store.close(SecretStoreOperation::Put);
    bool saved = false;
    REQUIRE(f.service->put(host_target(), bytes("hunter22"), std::nullopt, [&](Result<SecretState>) { saved = true; }));
    f.service.reset();
    f.store.open();
    f.settle();
    CHECK(f.stored(value_key(host_target())) == "hunter22");
    f.strand.advance(kStoreCallDeadline);
    CHECK_FALSE(saved);
}

TEST_CASE("an index past what one store value holds is kept in chunks", "[secrets][service]") {
    Fixture f;
    f.make();
    f.start();
    constexpr int kTargets = 60;
    for (int i = 0; i < kTargets; ++i) REQUIRE(f.put_saved(numbered_host_target(i), "hunter22").has_value());
    const std::optional<std::string> first = f.stored(index_key());
    const std::optional<std::string> second = f.stored(index_key() + ".1");
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->size() <= 2048);
    CHECK(second->size() <= 2048);
    CHECK(first->size() + second->size() > 2560);
    CHECK_FALSE(f.stored(index_key() + ".2").has_value());

    f.make();
    f.start();
    for (int i = 0; i < kTargets; ++i) CHECK(f.state(numbered_host_target(i)) == kRemembered);

    // A shorter index drops the chunks it no longer needs, and an empty one drops them all.
    for (int i = 0; i < kTargets - 1; ++i) REQUIRE(f.clear(numbered_host_target(i)).has_value());
    CHECK(f.stored(index_key()).has_value());
    CHECK_FALSE(f.stored(index_key() + ".1").has_value());
    f.make();
    f.start();
    CHECK(f.state(numbered_host_target(kTargets - 1)) == kRemembered);
    REQUIRE(f.clear(numbered_host_target(kTargets - 1)).has_value());
    CHECK(f.inner.keys().empty());
}

TEST_CASE("a line logged before a drop stays masked when the logger writes it later", "[secrets][service]") {
    const auto gate = std::make_shared<LogGate>();
    const auto written = std::make_shared<std::vector<std::string>>();
    Logger::add_sink(std::make_unique<GateSink>(gate, written));
    Logger::install(1 << 20);
    // Declared first, so it runs last, also when a REQUIRE fails.
    const struct LoggerStop {
        LogGate& gate;
        ~LoggerStop() {
            gate.open();
            Logger::shutdown();
        }
    } stop{*gate};
    Fixture f;
    std::optional<SecretService> service;
    service.emplace(f.store, f.store_worker, f.strand, f.timers, f.requests, f.events, Logger::redactor(), kRootHash);
    bool started = false;
    service->start([&](SecretsAvailability) { started = true; });
    f.strand.run_until([&] { return started; });
    std::optional<Result<SecretState>> saved;
    REQUIRE(service->put(host_target(), bytes("first-secret"), std::nullopt,
                         [&](Result<SecretState> result) { saved = std::move(result); }));
    f.strand.run_until([&] { return saved.has_value(); });

    gate->arm();
    Logger::write(LogLevel::Info, LogCategory::Engine, std::nullopt, "held by the writer");
    gate->wait_entered();
    REBOOT_LOG_INFO(Engine, "joining with {}", "first-secret");
    // Replacing the value drops the first one while its line is still queued.
    saved.reset();
    REQUIRE(service->put(host_target(), bytes("second-secret"), std::nullopt,
                         [&](Result<SecretState> result) { saved = std::move(result); }));
    f.strand.run_until([&] { return saved.has_value(); });
    gate->open();
    Logger::flush();

    CHECK(std::ranges::count(*written, std::string("joining with ***")) == 1);
    CHECK(std::ranges::none_of(*written, [](const std::string& line) { return line.contains("first-secret"); }));
    CHECK(Logger::redactor().apply("first-secret") == "first-secret");
    CHECK(Logger::redactor().apply("second-secret") == "***");
    service.reset();
}

TEST_CASE("a write after a load that found stale index lines never leaves a value unlisted", "[secrets][service]") {
    Fixture f;
    f.make();
    f.start();
    // 36 lines fill a chunk, so dropping the first moves the 37th down into chunk 0.
    for (int i = 1; i <= 40; ++i) REQUIRE(f.put_saved(numbered_host_target(i), "hunter22").has_value());
    REQUIRE(f.stored(index_key() + ".1").has_value());
    f.service.reset();
    REQUIRE(f.inner.erase(value_key(numbered_host_target(1))).has_value());

    ListingCheckStore checked(f.inner);
    std::optional<SecretService> service;
    service.emplace(checked, f.store_worker, f.strand, f.timers, f.requests, f.events, f.redactor, kRootHash);
    bool started = false;
    service->start([&](SecretsAvailability) { started = true; });
    f.strand.run_until([&] { return started; });
    std::optional<Result<SecretState>> saved;
    REQUIRE(service->put(numbered_host_target(41), bytes("hunter22"), std::nullopt,
                         [&](Result<SecretState> result) { saved = std::move(result); }));
    f.strand.run_until([&] { return saved.has_value(); });
    REQUIRE(saved->has_value());
    CHECK_FALSE(checked.unlisted.load());
    service.reset();

    // Every value is still listed, and the stale line is gone.
    f.make();
    f.start();
    for (int i = 2; i <= 41; ++i) CHECK(f.state(numbered_host_target(i)) == kRemembered);
    const std::optional<std::string> first = f.stored(index_key());
    REQUIRE(first);
    CHECK(first->find(numbered_host_target(1).scope.text()) == std::string::npos);
}
