#include "reboot/backend/remote_login.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/json/monotonic_resource.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>

#include "messages.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/secrets/secret_target.hpp"

namespace rb::backend {

namespace {

constexpr std::string_view kTokenPath = "/account/api/oauth/token";
constexpr std::string_view kExchangePath = "/account/api/oauth/exchange";
// The client the game itself authenticates as, so the upstream sees the login the front swaps in later.
constexpr std::string_view kGameClientAuthorization =
    "basic ZWM2ODRiOGM2ODdmNDc5ZmFkZWEzY2IyYWQ4M2Y1YzY6ZTFmMzFjMjExZjI4NDEzMTg2MjYyZDM3YTEzZmM4NGQ=";
constexpr std::size_t kMaxCredentialBody = 64u << 10;

[[nodiscard]] bool success(u32 status) noexcept { return status >= 200 && status < 300; }

// What an OAuth server answers for a wrong password.
[[nodiscard]] bool rejected(u32 status) noexcept { return status == 400 || status == 401 || status == 403; }

void append(std::vector<u8>& out, std::string_view text) { out.insert(out.end(), text.begin(), text.end()); }

// application/x-www-form-urlencoded.
void append_encoded(std::vector<u8>& out, std::string_view text) {
    constexpr std::string_view kHex = "0123456789ABCDEF";
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        const bool unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                                (byte >= '0' && byte <= '9') || byte == '-' || byte == '.' || byte == '_' || byte == '~';
        if (unreserved) {
            out.push_back(byte);
        } else if (byte == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(static_cast<u8>(kHex[byte >> 4]));
            out.push_back(static_cast<u8>(kHex[byte & 0x0F]));
        }
    }
}

[[nodiscard]] SecretBytes password_grant(std::string_view login, std::span<const u8> password) {
    std::vector<u8> body;
    // Sized for the worst case up front, so no reallocation leaves an unwiped copy behind.
    body.reserve(64 + 3 * (login.size() + password.size()));
    append(body, "grant_type=password&username=");
    append_encoded(body, login);
    append(body, "&password=");
    append_encoded(body, std::string_view(reinterpret_cast<const char*>(password.data()), password.size()));
    return SecretBytes(std::move(body));
}

// One string member of a JSON object body. The parse runs in a buffer that is wiped afterwards, so
// no copy of the credential outlives the call.
[[nodiscard]] std::optional<SecretString> secret_field(const SecretBytes& body, std::string_view key) {
    std::vector<unsigned char> arena(2 * body.reveal().size() + 1024);
    std::optional<SecretString> found;
    {
        boost::json::monotonic_resource resource(arena.data(), arena.size());
        boost::system::error_code error;
        const boost::json::value parsed = boost::json::parse(
            std::string_view(reinterpret_cast<const char*>(body.reveal().data()), body.reveal().size()), error,
            &resource);
        if (!error)
            if (const boost::json::object* object = parsed.if_object())
                if (const boost::json::value* value = object->if_contains(key); value != nullptr && value->is_string())
                    if (!value->get_string().empty()) found.emplace(std::string(value->get_string()));
    }
    secure_wipe(arena.data(), arena.size());
    return found;
}

[[nodiscard]] Diagnostic status_failure(MessageId message, const std::string& origin, u32 status) {
    return make_diag(ErrorDomain::Backend, message).arg("origin", origin).arg("status", status).retryable(status >= 500);
}

}  // namespace

struct RemoteLogin::Impl {
    struct Login {
        RemoteLoginRequest request;
        CancelToken token;
        UniqueFunction<void(Result<RemoteLoginResult>)> done;
        secrets::SecretTarget target;
        std::string origin;
    };

    void require(std::shared_ptr<Login> login, secrets::NeedsSecretReason reason) {
        secrets::SecretWait wait{login->request.wait.session, login->request.wait.op, reason};
        const std::optional<RequestId> raised = secrets.require(
            login->target, wait, login->token,
            [this, alive = alive.token(), login](Result<SecretBytes> password) mutable {
                if (alive.cancelled()) return;
                if (!password) return login->done(std::unexpected(std::move(password.error())));
                grant(std::move(login), *password);
            });
        if (raised && login->request.awaiting_user) login->request.awaiting_user(*raised);
    }

    void grant(std::shared_ptr<Login> login, const SecretBytes& password) {
        net::HttpRequest request;
        request.method = net::HttpMethod::Post;
        request.url = login->origin + std::string(kTokenPath);
        request.headers = {{"Content-Type", "application/x-www-form-urlencoded"},
                           {"Authorization", std::string(kGameClientAuthorization)}};
        request.retry = net::kNoRetry;
        request.max_body = kMaxCredentialBody;
        net::HttpSecrets secret;
        secret.body = password_grant(login->request.login, password.reveal());
        const CancelToken token = login->token;
        Result<void> sent = http.send_secret(
            std::move(request), std::move(secret), token,
            [this, alive = alive.token(), login](Result<net::SecretHttpResponse> response) mutable {
                if (alive.cancelled()) return;
                if (!response) return login->done(std::unexpected(std::move(response.error())));
                on_granted(std::move(login), *response);
            });
        if (!sent) login->done(std::unexpected(std::move(sent.error())));
    }

    void on_granted(std::shared_ptr<Login> login, const net::SecretHttpResponse& response) {
        if (rejected(response.status)) return require(std::move(login), secrets::NeedsSecretReason::Rejected);
        if (!success(response.status))
            return login->done(std::unexpected(status_failure(msg::kRemoteLoginFailed, login->origin, response.status)));
        const bool exchange =
            login->request.want_exchange_code && login->request.upstream.flavor == identity::UpstreamFlavor::Reboot;
        if (!exchange) return login->done(RemoteLoginResult{});
        std::optional<SecretString> access_token = secret_field(response.body, "access_token");
        if (!access_token)
            return login->done(std::unexpected(status_failure(msg::kRemoteLoginFailed, login->origin, response.status)));
        request_exchange(std::move(login), std::move(*access_token));
    }

    void request_exchange(std::shared_ptr<Login> login, SecretString access_token) {
        net::HttpRequest request;
        request.url = login->origin + std::string(kExchangePath);
        request.retry = net::kNoRetry;
        request.max_body = kMaxCredentialBody;
        net::HttpSecrets secret;
        secret.headers.push_back({"Authorization", SecretString("bearer " + access_token.reveal())});
        const CancelToken token = login->token;
        Result<void> sent = http.send_secret(
            std::move(request), std::move(secret), token,
            [this, alive = alive.token(), login](Result<net::SecretHttpResponse> response) mutable {
                if (alive.cancelled()) return;
                if (!response) return login->done(std::unexpected(std::move(response.error())));
                std::optional<SecretString> code =
                    success(response->status) ? secret_field(response->body, "code") : std::nullopt;
                if (!code)
                    return login->done(
                        std::unexpected(status_failure(msg::kExchangeCodeFailed, login->origin, response->status)));
                const std::string& value = code->reveal();
                redactor.add_secret(std::span(reinterpret_cast<const u8*>(value.data()), value.size()));
                login->done(RemoteLoginResult{std::move(*code)});
            });
        if (!sent) login->done(std::unexpected(std::move(sent.error())));
    }

    net::HttpClient& http;
    secrets::SecretService& secrets;
    Redactor& redactor;
    CancelSource alive;
};

RemoteLogin::RemoteLogin(net::HttpClient& http, secrets::SecretService& secrets, Redactor& redactor)
    : impl_(new Impl{http, secrets, redactor, {}}) {}

RemoteLogin::~RemoteLogin() { impl_->alive.cancel(CancelReason::Shutdown); }

void RemoteLogin::login(RemoteLoginRequest request, CancelToken token, UniqueFunction<void(Result<RemoteLoginResult>)> done) {
    secrets::SecretTarget target{secrets::SecretKind::RemoteBackendPassword,
                                 secrets::SecretScope::backend(request.upstream.url.endpoint())};
    std::string origin = request.upstream.url.origin();
    const secrets::NeedsSecretReason reason = request.wait.reason;
    auto login = std::make_shared<Impl::Login>(
        Impl::Login{std::move(request), std::move(token), std::move(done), std::move(target), std::move(origin)});
    impl_->require(std::move(login), reason);
}

}  // namespace rb::backend
