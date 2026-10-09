#pragma once

#include <openssl/evp.h>

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/trust/key_ring.hpp"
#include "reboot/trust/signed_document.hpp"
#include "reboot/trust/signed_document_kind.hpp"

namespace rb::catalog::test {

[[nodiscard]] inline std::vector<u8> bytes_of(std::string_view text) { return {text.begin(), text.end()}; }

struct PkeyDeleter {
    void operator()(EVP_PKEY* key) const noexcept { EVP_PKEY_free(key); }
};
struct MdCtxDeleter {
    void operator()(EVP_MD_CTX* context) const noexcept { EVP_MD_CTX_free(context); }
};

// Signs the way the catalog generator does: the BuildCatalog context prefix, then the body.
class TestSigner {
public:
    TestSigner() : key_(EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519")) {
        REQUIRE(key_);
        std::size_t size = public_key_.size();
        REQUIRE(EVP_PKEY_get_raw_public_key(key_.get(), public_key_.data(), &size) == 1);
    }

    [[nodiscard]] const trust::Ed25519PublicKey& public_key() const { return public_key_; }

    [[nodiscard]] std::string signature_file(std::string_view body) const {
        const std::string_view prefix = trust::signature_context(trust::SignedDocumentKind::BuildCatalog);
        std::vector<u8> message(prefix.begin(), prefix.end());
        message.insert(message.end(), body.begin(), body.end());

        const std::unique_ptr<EVP_MD_CTX, MdCtxDeleter> context(EVP_MD_CTX_new());
        REQUIRE(EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key_.get()) == 1);
        trust::Ed25519Signature signature{};
        std::size_t size = signature.size();
        REQUIRE(EVP_DigestSign(context.get(), signature.data(), &size, message.data(), message.size()) == 1);

        constexpr std::string_view kHex = "0123456789abcdef";
        std::string hex;
        for (const u8 byte : signature) {
            hex += kHex[byte >> 4];
            hex += kHex[byte & 0xf];
        }
        return "ed25519 " + trust::key_id_of(public_key_) + " " + hex + "\n";
    }

    [[nodiscard]] trust::KeyRing ring() const {
        return trust::KeyRing(trust::SignedDocumentKind::BuildCatalog, {public_key_, {}});
    }

private:
    std::unique_ptr<EVP_PKEY, PkeyDeleter> key_;
    trust::Ed25519PublicKey public_key_{};
};

inline constexpr u64 kFarFutureUnixMs = 4'000'000'000'000;

// Two entries: "12.41" is installable, "cert" (alias "3.50.1") is not.
[[nodiscard]] inline std::string catalog_json(u64 serial, u64 schema = 1, u64 expires_unix_ms = kFarFutureUnixMs) {
    return R"({"schema":)" + std::to_string(schema) + R"(,"serial":)" + std::to_string(serial) +
           R"(,"generated_unix_ms":0,"expires_unix_ms":)" + std::to_string(expires_unix_ms) + R"(,"entries":[)"
           R"({"id":"12.41","version":"12.41","url":"https://builds.test/12.41.zip","format":"zip","container":"none","archive_size":10,"availability":"available"},)"
           R"({"id":"cert","version":"3.5","aliases":["3.50.1"],"url":"https://builds.test/cert.zip","format":"zip","container":"none","archive_size":10,"availability":"withdrawn"}],)"
           R"("flag_ranges":[{"first":"1.0","last":"30.10"}]})";
}

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

    // Runs what is ready, including what that posts, without waiting for other threads.
    std::size_t run_ready() {
        std::size_t ran = 0;
        while (UniqueFunction<void()> task = next(false)) {
            task();
            ++ran;
        }
        return ran;
    }

    // Runs tasks, waiting for other threads to post, until `done` holds.
    template <class Done>
    void run_until(Done&& done) {
        run_ready();
        while (!done()) {
            UniqueFunction<void()> task = next(true);
            REQUIRE(static_cast<bool>(task));
            task();
            run_ready();
        }
    }

    [[nodiscard]] std::size_t timed_pending() {
        const std::scoped_lock lock(mutex_);
        return timed_.size();
    }

    // Moves time one due task at a time, so each timer runs at its own deadline.
    void advance(std::chrono::steady_clock::duration by) {
        const SteadyTime end = clock_.steady_now() + by;
        run_ready();
        while (true) {
            SteadyTime due{};
            {
                const std::scoped_lock lock(mutex_);
                if (timed_.empty() || timed_.begin()->first > end) break;
                due = timed_.begin()->first;
            }
            if (due > clock_.steady_now()) clock_.advance(due - clock_.steady_now());
            run_ready();
        }
        if (end > clock_.steady_now()) clock_.advance(end - clock_.steady_now());
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
        // A bound, not a sleep: a missing post fails the test instead of hanging it.
        if (wait && !posted_.wait_for(lock, std::chrono::seconds{20}, [this] { return !ready_.empty(); })) return {};
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

}  // namespace rb::catalog::test
