#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/ports/secret_store.hpp"
#include "reboot/secrets/secret_service.hpp"
#include "reboot/secrets/secret_target.hpp"
#include "reboot/testing/fake_secret_store.hpp"

namespace reboot::secrets::test {

inline constexpr std::string_view kRootHash = "00112233aabbccdd";

// The strand beside a real WorkerPool: workers post from their threads, timed tasks follow the
// ManualClock, and only the test thread runs anything.
class TestStrand final : public Executor {
public:
    explicit TestStrand(ManualClock& clock) : clock_(clock) {}

    void post(UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        ready_.push_back(std::move(task));
        posted_.notify_one();
    }
    void post_at(SteadyTime when, UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        timed_.emplace(when, std::move(task));
    }

    // Runs what is ready, including what that posts, without waiting for workers.
    std::size_t run_ready() {
        std::size_t ran = 0;
        while (UniqueFunction<void()> task = next(false)) {
            task();
            ++ran;
        }
        return ran;
    }

    // Waits for the next task, posted by a worker, and runs it.
    void run_next() {
        UniqueFunction<void()> task = next(true);
        REQUIRE(static_cast<bool>(task));
        task();
    }

    template <class Done>
    void run_until(Done&& done) {
        run_ready();
        while (!done()) run_next();
    }

    void advance(std::chrono::steady_clock::duration by) {
        clock_.advance(by);
        run_ready();
    }

private:
    UniqueFunction<void()> next(bool wait) {
        std::unique_lock lock(mutex_);
        const SteadyTime now = clock_.steady_now();
        while (!timed_.empty() && timed_.begin()->first <= now) {
            ready_.push_back(std::move(timed_.begin()->second));
            timed_.erase(timed_.begin());
        }
        // A bound, not a sleep: a missing reply fails the test instead of hanging it.
        if (wait && !posted_.wait_for(lock, std::chrono::seconds{10}, [this] { return !ready_.empty(); })) return {};
        if (ready_.empty()) return {};
        UniqueFunction<void()> task = std::move(ready_.front());
        ready_.pop_front();
        return task;
    }

    ManualClock& clock_;
    std::mutex mutex_;
    std::condition_variable posted_;
    std::deque<UniqueFunction<void()>> ready_;
    std::multimap<SteadyTime, UniqueFunction<void()>> timed_;
};

// Blocks every call of a closed operation until open(), like a store waiting on a prompt.
class GatedStore final : public ports::ISecretStore {
public:
    explicit GatedStore(testing::FakeSecretStore& inner) : inner_(inner) {}

    [[nodiscard]] ports::SecretStoreKind kind() const override { return inner_.kind(); }
    Result<void> put(std::string_view key, std::span<const u8> value) override {
        pass(testing::SecretStoreOperation::Put);
        return inner_.put(key, value);
    }
    Result<std::optional<SecretBytes>> get(std::string_view key) override {
        pass(testing::SecretStoreOperation::Get);
        return inner_.get(key);
    }
    Result<void> erase(std::string_view key) override {
        pass(testing::SecretStoreOperation::Erase);
        return inner_.erase(key);
    }

    void close(testing::SecretStoreOperation operation) {
        const std::scoped_lock lock(mutex_);
        closed_ = operation;
    }
    void open() {
        {
            const std::scoped_lock lock(mutex_);
            closed_.reset();
        }
        opened_.notify_all();
    }

private:
    void pass(testing::SecretStoreOperation operation) {
        std::unique_lock lock(mutex_);
        opened_.wait(lock, [&] { return closed_ != operation; });
    }

    testing::FakeSecretStore& inner_;
    std::mutex mutex_;
    std::condition_variable opened_;
    std::optional<testing::SecretStoreOperation> closed_;
};

[[nodiscard]] inline SecretBytes bytes(std::string_view text) { return SecretBytes(std::vector<u8>(text.begin(), text.end())); }

[[nodiscard]] inline std::string text_of(const SecretBytes& value) {
    return {value.reveal().begin(), value.reveal().end()};
}

[[nodiscard]] inline Diagnostic store_fault() {
    return make_diag(ErrorDomain::Platform, MessageId{"platform.secret_store_failed"});
}

[[nodiscard]] inline SecretTarget remote_target() {
    return SecretTarget::parse(SecretKind::RemoteBackendPassword, "backend.example.com").value();
}

[[nodiscard]] inline SecretTarget host_target() {
    return SecretTarget::parse(SecretKind::HostJoinPassword, "0a1b2c3d-4e5f-6071-8293-a4b5c6d7e8f9").value();
}

[[nodiscard]] inline SecretTarget join_target(RequestId request) {
    return SecretTarget{SecretKind::JoinPassword, SecretScope::join_request(request)};
}

[[nodiscard]] inline std::string value_key(const SecretTarget& target, std::string_view root = kRootHash) {
    return std::string(root) + "/" + std::string(kind_name(target.kind)) + "/" + target.scope.text();
}

[[nodiscard]] inline std::string index_key(std::string_view root = kRootHash) { return std::string(root) + "/index"; }

struct Fixture {
    explicit Fixture(ports::SecretStoreKind kind = ports::SecretStoreKind::Os) : inner(kind) {}
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    // A worker still blocked in the store would keep the pool from joining.
    ~Fixture() {
        store.open();
        service.reset();
    }

    SecretService& make(std::string_view root = kRootHash) {
        service.emplace(store, workers, strand, timers, requests, events, redactor, root);
        return *service;
    }

    SecretsAvailability start() {
        std::optional<SecretsAvailability> availability;
        service->start([&](SecretsAvailability result) { availability = result; });
        strand.run_until([&] { return availability.has_value(); });
        return *availability;
    }

    // Puts and waits for `saved`.
    Result<SecretState> put_saved(const SecretTarget& target, std::string_view value,
                                  std::optional<Retention> retention = std::nullopt) {
        std::optional<Result<SecretState>> saved;
        const Result<void> accepted =
            service->put(target, bytes(value), retention, [&](Result<SecretState> result) { saved = std::move(result); });
        REQUIRE(accepted.has_value());
        strand.run_until([&] { return saved.has_value(); });
        return std::move(*saved);
    }

    Result<void> clear(const SecretTarget& target) {
        std::optional<Result<void>> done;
        service->clear(target, [&](Result<void> result) { done = std::move(result); });
        strand.run_until([&] { return done.has_value(); });
        return std::move(*done);
    }

    [[nodiscard]] std::optional<std::string> stored(const std::string& key) {
        Result<std::optional<SecretBytes>> value = inner.get(key);
        REQUIRE(value.has_value());
        if (!*value) return std::nullopt;
        return text_of(**value);
    }

    // One worker runs jobs in order, so once this sentinel replies every earlier store call has
    // replied too, also one abandoned at its deadline.
    void settle() {
        bool settled = false;
        workers.submit<void>([](CancelToken) -> Result<void> { return {}; }, CancelToken{}, strand,
                             [&](Result<void>) { settled = true; });
        strand.run_until([&] { return settled; });
    }

    [[nodiscard]] SecretState state(const SecretTarget& target) const {
        const Result<SecretState> current = service->state(target);
        REQUIRE(current.has_value());
        return *current;
    }

    ManualClock clock;
    TestStrand strand{clock};
    testing::FakeSecretStore inner;
    GatedStore store{inner};
    Redactor redactor;
    EventBus events{EngineEpoch{1}};
    UserRequestRegistry requests{events};
    TimerService timers{clock, strand};
    WorkerPool workers{1};
    std::optional<SecretService> service;
};

}  // namespace reboot::secrets::test
