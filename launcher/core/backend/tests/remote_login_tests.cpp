#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/backend/remote_login.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/net/host_tls_memory.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/secrets/needs_secret.hpp"
#include "reboot/secrets/secret_service.hpp"
#include "reboot/testing/fake_http_transport.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/fake_secret_store.hpp"

using namespace reboot;
using namespace reboot::backend;

namespace {

// The strand beside SecretService's real WorkerPool: workers post from their threads and only the
// test thread runs anything.
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

    void run_ready() {
        while (UniqueFunction<void()> task = next(false)) task();
    }

    template <class Done>
    void run_until(Done&& done) {
        run_ready();
        while (!done()) {
            UniqueFunction<void()> task = next(true);
            REQUIRE(static_cast<bool>(task));
            task();
        }
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

[[nodiscard]] SecretBytes bytes(std::string_view text) { return SecretBytes(std::vector<u8>(text.begin(), text.end())); }

[[nodiscard]] std::string text(const std::vector<u8>& data) { return {data.begin(), data.end()}; }

[[nodiscard]] testing::FakeHttpResponse answer(u32 status, std::string body = {}) {
    testing::FakeHttpResponse response;
    response.status = status;
    response.body.assign(body.begin(), body.end());
    return response;
}

[[nodiscard]] const std::string* header(const ports::HttpRequest& request, std::string_view name) {
    for (const ports::HttpHeader& entry : request.headers)
        if (entry.name == name) return &entry.value;
    return nullptr;
}

constexpr std::string_view kTokenUrl = "https://play.example:443/account/api/oauth/token";
constexpr std::string_view kExchangeUrl = "https://play.example:443/account/api/oauth/exchange";

struct Login {
    Login() {
        std::optional<secrets::SecretsAvailability> started;
        secrets.start([&](secrets::SecretsAvailability availability) { started = availability; });
        strand.run_until([&] { return started.has_value(); });
    }

    [[nodiscard]] static secrets::SecretTarget target() {
        return secrets::SecretTarget{secrets::SecretKind::RemoteBackendPassword,
                                     secrets::SecretScope::backend(HostPort{"play.example", Port{443}})};
    }

    void put(std::string_view password) {
        std::optional<Result<secrets::SecretState>> saved;
        REQUIRE(secrets.put(target(), bytes(password), std::nullopt,
                            [&](Result<secrets::SecretState> state) { saved = std::move(state); }));
        strand.run_until([&] { return saved.has_value(); });
    }

    [[nodiscard]] RemoteLoginRequest request(identity::UpstreamFlavor flavor, bool exchange) {
        RemoteLoginRequest request;
        request.upstream = BackendInfo{.url = BackendUrl{net::UrlScheme::Https, "play.example", Port{443}}, .flavor = flavor};
        request.login = "player@example.com";
        request.want_exchange_code = exchange;
        request.wait = secrets::SecretWait{std::nullopt, OpId{4}, secrets::NeedsSecretReason::Missing};
        request.awaiting_user = [this](RequestId id) { awaited.push_back(id); };
        return request;
    }

    void start(RemoteLoginRequest request, CancelToken token = {}) {
        login.login(std::move(request), std::move(token), [this](Result<RemoteLoginResult> outcome) {
            ++calls;
            result = std::move(outcome);
        });
    }

    void finish() {
        strand.run_until([&] { return result.has_value(); });
    }

    [[nodiscard]] std::optional<secrets::NeedsSecret> needs_secret(RequestId id) const {
        for (const UserRequest& pending : requests.pending())
            if (pending.id == id) return std::any_cast<secrets::NeedsSecret>(pending.payload);
        return std::nullopt;
    }

    ManualClock clock;
    TestStrand strand{clock};
    TimerService timers{clock, strand};
    EventBus events{EngineEpoch{1}};
    UserRequestRegistry requests{events};
    testing::FakeSecretStore store{ports::SecretStoreKind::Unavailable};
    Redactor redactor;
    WorkerPool workers{1};
    secrets::SecretService secrets{store, workers, strand, timers, requests, events, redactor, "00112233aabbccdd"};
    testing::FakeHttpTransport transport{strand, clock};
    testing::FakeRandom random{5};
    net::HostTlsMemory tls{{}, nullptr};
    net::HttpClient http{transport, tls, strand, timers, random};
    RemoteLogin login{http, secrets, redactor};

    std::vector<RequestId> awaited;
    std::optional<Result<RemoteLoginResult>> result;
    int calls = 0;
};

}  // namespace

TEST_CASE("a Reboot upstream logs in with a password grant and hands out an exchange code", "[backend][login]") {
    Login l;
    l.put("p&ss w");
    l.transport.route("POST", std::string(kTokenUrl), answer(200, R"({"access_token":"eg1~token","expires_in":28800})"));
    l.transport.route("GET", std::string(kExchangeUrl), answer(200, R"({"expiresInSeconds":300,"code":"code-1234"})"));
    l.start(l.request(identity::UpstreamFlavor::Reboot, true));
    l.finish();
    REQUIRE(l.result->has_value());
    REQUIRE((*l.result)->exchange_code);
    CHECK((*l.result)->exchange_code->reveal() == "code-1234");
    CHECK(l.redactor.apply("argv code-1234").find("code-1234") == std::string::npos);
    CHECK(l.awaited.empty());

    const std::vector<ports::HttpRequest> sent = l.transport.requests();
    REQUIRE(sent.size() == 2);
    CHECK(sent[0].method == "POST");
    CHECK(text(sent[0].body) == "grant_type=password&username=player%40example.com&password=p%26ss+w");
    REQUIRE(header(sent[0], "Content-Type"));
    CHECK(*header(sent[0], "Content-Type") == "application/x-www-form-urlencoded");
    REQUIRE(header(sent[0], "Authorization"));
    CHECK(header(sent[0], "Authorization")->starts_with("basic "));
    REQUIRE(header(sent[1], "Authorization"));
    CHECK(*header(sent[1], "Authorization") == "bearer eg1~token");
}

TEST_CASE("a third-party upstream only checks the password", "[backend][login]") {
    Login l;
    l.put("hunter22");
    l.transport.route("POST", std::string(kTokenUrl), answer(200, "{}"));
    l.start(l.request(identity::UpstreamFlavor::ThirdParty, true));
    l.finish();
    REQUIRE(l.result->has_value());
    CHECK_FALSE((*l.result)->exchange_code);
    CHECK(l.transport.requests().size() == 1);
}

TEST_CASE("a missing password raises NeedsSecret, and a rejected one raises it again", "[backend][login]") {
    Login l;
    l.transport.route_sequence("POST", std::string(kTokenUrl), {answer(401, "{}"), answer(200, "{}")});
    l.start(l.request(identity::UpstreamFlavor::ThirdParty, false));
    l.strand.run_ready();
    REQUIRE(l.awaited.size() == 1);
    const std::optional<secrets::NeedsSecret> missing = l.needs_secret(l.awaited[0]);
    REQUIRE(missing);
    CHECK(missing->reason == secrets::NeedsSecretReason::Missing);
    CHECK(missing->target == Login::target());

    l.put("wrong-password");
    REQUIRE(l.requests.respond(l.awaited[0], secrets::SecretProvided{}));
    l.strand.run_until([&] { return l.awaited.size() == 2; });
    const std::optional<secrets::NeedsSecret> rejected = l.needs_secret(l.awaited[1]);
    REQUIRE(rejected);
    CHECK(rejected->reason == secrets::NeedsSecretReason::Rejected);
    CHECK(l.calls == 0);

    l.put("right-password");
    REQUIRE(l.requests.respond(l.awaited[1], secrets::SecretProvided{}));
    l.finish();
    REQUIRE(l.result->has_value());
    REQUIRE(l.transport.requests().size() == 2);
    CHECK(text(l.transport.requests()[1].body).ends_with("password=right-password"));
}

TEST_CASE("upstream failures name the origin and status", "[backend][login]") {
    Login l;
    l.put("hunter22");
    l.transport.route("POST", std::string(kTokenUrl), answer(503));
    l.start(l.request(identity::UpstreamFlavor::Reboot, true));
    l.finish();
    REQUIRE_FALSE(l.result->has_value());
    CHECK(l.result->error().id == "backend.remote_login_failed");
    CHECK(l.result->error().retryable);

    Login m;
    m.put("hunter22");
    m.transport.route("POST", std::string(kTokenUrl), answer(200, R"({"access_token":"eg1~token"})"));
    m.transport.route("GET", std::string(kExchangeUrl), answer(200, "{}"));
    m.start(m.request(identity::UpstreamFlavor::Reboot, true));
    m.finish();
    REQUIRE_FALSE(m.result->has_value());
    CHECK(m.result->error().id == "backend.exchange_code_failed");
}

TEST_CASE("cancelling a login withdraws its NeedsSecret", "[backend][login]") {
    Login l;
    CancelSource cancel;
    l.start(l.request(identity::UpstreamFlavor::ThirdParty, false), cancel.token());
    l.strand.run_ready();
    REQUIRE(l.awaited.size() == 1);
    cancel.cancel(CancelReason::User);
    l.finish();
    REQUIRE_FALSE(l.result->has_value());
    CHECK(l.result->error().id == "secrets.request_withdrawn");
    CHECK(l.requests.pending().empty());
}
