#include "reboot/backend/remote_backend_probe.hpp"

#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>

#include "messages.hpp"
#include "reboot/net/http_client.hpp"

namespace reboot::backend {

namespace {

constexpr std::string_view kRebootImpl = "reboot";
constexpr std::size_t kMaxInfoBody = 64u << 10;

[[nodiscard]] bool refused_by_tls_memory(const Diagnostic& error) noexcept {
    return error.id == "net.plain_http_needs_consent" || error.id == "net.https_downgrade_refused";
}

template <class N>
[[nodiscard]] std::optional<N> number_field(const boost::json::object& object, std::string_view key, u64 min, u64 max) {
    const boost::json::value* value = object.if_contains(key);
    if (value == nullptr) return std::nullopt;
    u64 number = 0;
    if (const auto* unsigned_number = value->if_uint64()) number = *unsigned_number;
    else if (const auto* signed_number = value->if_int64(); signed_number != nullptr && *signed_number >= 0)
        number = static_cast<u64>(*signed_number);
    else return std::nullopt;
    if (number < min || number > max) return std::nullopt;
    return static_cast<N>(number);
}

// Anything but a {impl: "reboot"} object is a third-party backend that still answered.
[[nodiscard]] BackendInfo read_info(BackendUrl url, const std::vector<u8>& body) {
    BackendInfo info{.url = std::move(url)};
    boost::system::error_code error;
    const boost::json::value parsed =
        boost::json::parse(std::string_view(reinterpret_cast<const char*>(body.data()), body.size()), error);
    const boost::json::object* object = error ? nullptr : parsed.if_object();
    if (object == nullptr) return info;
    const boost::json::value* impl = object->if_contains("impl");
    if (impl == nullptr || !impl->is_string() || impl->get_string() != kRebootImpl) return info;
    info.flavor = identity::UpstreamFlavor::Reboot;
    if (const boost::json::value* version = object->if_contains("version"); version != nullptr && version->is_string())
        info.version = std::string(version->get_string());
    info.api_version = number_field<u32>(*object, "api_version", 0, std::numeric_limits<u32>::max());
    if (const auto port = number_field<u16>(*object, "ws_port", 1, std::numeric_limits<u16>::max()))
        info.ws_port = Port{*port};
    return info;
}

}  // namespace

struct RemoteBackendProbe::Impl {
    struct Attempt {
        BackendUrl url;
        CancelToken token;
        UniqueFunction<void(Result<BackendInfo>)> done;
        // Schemes still to try after the current one.
        std::vector<net::UrlScheme> fallbacks;
        std::vector<Diagnostic> failures;
    };

    void run(Attempt attempt) {
        net::HttpRequest request;
        request.url = attempt.url.origin() + std::string(kBackendInfoPath);
        request.retry = net::kNoRetry;
        request.max_body = kMaxInfoBody;
        auto shared = std::make_shared<Attempt>(std::move(attempt));
        Result<void> sent =
            http.send(std::move(request), shared->token,
                      [this, alive = alive.token(), shared](Result<net::HttpResponse> response) mutable {
                          if (alive.cancelled()) return;
                          if (response) return shared->done(read_info(std::move(shared->url), response->body));
                          next(std::move(*shared), std::move(response.error()));
                      });
        if (!sent) {
            // HostTlsMemory's verdict goes back as is, so the caller can ask the user.
            if (refused_by_tls_memory(sent.error())) return shared->done(std::unexpected(std::move(sent.error())));
            next(std::move(*shared), std::move(sent.error()));
        }
    }

    void next(Attempt attempt, Diagnostic failure) {
        if (attempt.token.cancelled()) return attempt.done(std::unexpected(std::move(failure)));
        const std::string origin = attempt.url.origin();
        attempt.failures.push_back(std::move(failure));
        if (!attempt.fallbacks.empty()) {
            attempt.url.scheme = attempt.fallbacks.front();
            attempt.fallbacks.erase(attempt.fallbacks.begin());
            return run(std::move(attempt));
        }
        DiagBuilder unreachable = make_diag(ErrorDomain::Backend, msg::kUnreachable).arg("origin", origin).retryable();
        for (Diagnostic& cause : attempt.failures) std::move(unreachable).cause(std::move(cause));
        attempt.done(std::move(unreachable).fail());
    }

    net::HttpClient& http;
    CancelSource alive;
};

RemoteBackendProbe::RemoteBackendProbe(net::HttpClient& http) : impl_(new Impl{http, {}}) {}

RemoteBackendProbe::~RemoteBackendProbe() { impl_->alive.cancel(CancelReason::Shutdown); }

void RemoteBackendProbe::probe(const BackendUrl& url, CancelToken token, UniqueFunction<void(Result<BackendInfo>)> done) {
    Impl::Attempt attempt{url, std::move(token), std::move(done), {}, {}};
    if (!url.scheme) {
        attempt.url.scheme = net::UrlScheme::Https;
        attempt.fallbacks.push_back(net::UrlScheme::Http);
    }
    impl_->run(std::move(attempt));
}

}  // namespace reboot::backend
