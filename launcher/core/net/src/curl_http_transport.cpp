#include "reboot/net/curl_http_transport.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <curl/curl.h>

#include "messages.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/net/http_error.hpp"
#include "reboot/ports/os_services.hpp"
#include "url.hpp"

namespace rb::net {

namespace {

constexpr long kMaxRedirects = 10;
// A wake-up only needs to bound how late a cancel or a new transfer is noticed.
constexpr int kPollTimeoutMs = 1000;

struct Transfer {
    Transfer() = default;
    Transfer(const Transfer&) = delete;
    Transfer& operator=(const Transfer&) = delete;
    ~Transfer() {
        if (easy != nullptr) curl_easy_cleanup(easy);
        wipe_request();
    }

    // Header values and bodies may carry credentials from HttpClient::send_secret.
    void wipe_request() noexcept {
        for (curl_slist* node = header_list; node != nullptr; node = node->next)
            if (node->data != nullptr) secure_wipe(node->data, std::strlen(node->data));
        if (header_list != nullptr) curl_slist_free_all(header_list);
        header_list = nullptr;
        for (ports::HttpHeader& header : request.headers) secure_wipe(header.value.data(), header.value.size());
        secure_wipe(request.body.data(), request.body.size());
    }

    u64 id = 0;
    CURL* easy = nullptr;
    curl_slist* header_list = nullptr;
    ports::HttpRequest request;
    ports::HttpCallbacks callbacks;
    CancelRegistration cancel;
    // Set before the cancel request is queued, so a cancel that beats the transfer onto the thread still counts.
    std::shared_ptr<std::atomic<bool>> cancel_flag = std::make_shared<std::atomic<bool>>(false);
    ParsedUrl url;
    std::vector<ports::HttpHeader> response_headers;
    bool headers_delivered = false;
    bool aborted_by_receiver = false;
    bool callback_threw = false;
    std::array<char, CURL_ERROR_SIZE> error_buffer{};
};

using TransferPtr = std::unique_ptr<Transfer>;

[[nodiscard]] Diagnostic http_unavailable(std::string detail) {
    return make_diag(ErrorDomain::Net, kHttpUnavailable).detail(std::move(detail));
}

[[nodiscard]] u32 response_code(CURL* easy) {
    long code = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);
    return code > 0 ? static_cast<u32>(code) : 0;
}

void deliver_headers(Transfer& transfer) {
    if (transfer.headers_delivered) return;
    transfer.headers_delivered = true;
    if (transfer.callbacks.on_headers)
        transfer.callbacks.on_headers(ports::HttpStatus{response_code(transfer.easy)}, transfer.response_headers);
}

void add_header_line(Transfer& transfer, std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
    // Each response of a redirect chain starts over; only the last one is reported.
    if (line.starts_with("HTTP/")) {
        transfer.response_headers.clear();
        return;
    }
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos || colon == 0) return;
    std::string_view value = line.substr(colon + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
    transfer.response_headers.push_back({std::string(line.substr(0, colon)), std::string(value)});
}

// curl is C: nothing may unwind through it, so a throwing callback aborts the transfer instead.
std::size_t on_header_line(char* data, std::size_t size, std::size_t count, void* user) noexcept {
    auto& transfer = *static_cast<Transfer*>(user);
    const std::size_t length = size * count;
    try {
        add_header_line(transfer, std::string_view(data, length));
    } catch (...) {
        transfer.callback_threw = true;
        return 0;
    }
    return length;
}

std::size_t on_body(char* data, std::size_t size, std::size_t count, void* user) noexcept {
    auto& transfer = *static_cast<Transfer*>(user);
    const std::size_t length = size * count;
    try {
        deliver_headers(transfer);
        if (length == 0 || !transfer.callbacks.on_body_chunk) return length;
        if (!transfer.callbacks.on_body_chunk(std::span<const u8>(reinterpret_cast<const u8*>(data), length))) {
            transfer.aborted_by_receiver = true;
            return CURL_WRITEFUNC_ERROR;
        }
    } catch (...) {
        transfer.callback_threw = true;
        return CURL_WRITEFUNC_ERROR;
    }
    return length;
}

[[nodiscard]] HttpErrorCode timeout_kind(const Transfer& transfer) {
    curl_off_t connect_us = 0;
    curl_off_t total_us = 0;
    curl_easy_getinfo(transfer.easy, CURLINFO_CONNECT_TIME_T, &connect_us);
    curl_easy_getinfo(transfer.easy, CURLINFO_TOTAL_TIME_T, &total_us);
    const auto total = transfer.request.total_timeout;
    const bool total_ran_out =
        total > std::chrono::milliseconds::zero() && total_us / 1000 + 50 >= static_cast<curl_off_t>(total.count());
    if (connect_us == 0) return total_ran_out && total < transfer.request.connect_timeout ? HttpErrorCode::TotalTimeout
                                                                                          : HttpErrorCode::ConnectTimeout;
    if (total_ran_out || !transfer.request.stall) return HttpErrorCode::TotalTimeout;
    return HttpErrorCode::Stalled;
}

[[nodiscard]] Diagnostic transfer_error(const Transfer& transfer, CURLcode code) {
    if (transfer.callback_threw) return internal_bug("curl_http_transport");
    HttpError error;
    error.host = transfer.url.host;
    switch (code) {
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY: error.code = HttpErrorCode::Dns; break;
        case CURLE_COULDNT_CONNECT: error.code = HttpErrorCode::Connect; break;
        case CURLE_OPERATION_TIMEDOUT: error.code = timeout_kind(transfer); break;
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_SSL_CIPHER:
        case CURLE_SSL_CACERT_BADFILE:
        case CURLE_SSL_ISSUER_ERROR:
        case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
        case CURLE_SSL_INVALIDCERTSTATUS:
        case CURLE_SSL_CRL_BADFILE:
        case CURLE_SSL_ENGINE_NOTFOUND:
        case CURLE_SSL_ENGINE_SETFAILED:
        case CURLE_SSL_ENGINE_INITFAILED: error.code = HttpErrorCode::Tls; break;
        // Only a redirect can reach a protocol outside the allowed set.
        case CURLE_UNSUPPORTED_PROTOCOL:
            error.code = transfer.url.scheme == UrlScheme::Https ? HttpErrorCode::DowngradeRefused : HttpErrorCode::Transport;
            break;
        case CURLE_URL_MALFORMAT:
            error.code = HttpErrorCode::InvalidUrl;
            error.host = transfer.request.url;
            break;
        default: error.code = HttpErrorCode::Transport; break;
    }
    if (error.code == HttpErrorCode::ConnectTimeout) error.limit = transfer.request.connect_timeout;
    if (error.code == HttpErrorCode::TotalTimeout) error.limit = transfer.request.total_timeout;
    if (error.code == HttpErrorCode::Stalled && transfer.request.stall) error.limit = transfer.request.stall->window;
    std::string detail = curl_easy_strerror(code);
    if (transfer.error_buffer[0] != '\0') detail += std::string(": ") + transfer.error_buffer.data();
    if (transfer.aborted_by_receiver) detail = "aborted by the receiver";
    error.detail = std::move(detail);
    long os_errno = 0;
    if (curl_easy_getinfo(transfer.easy, CURLINFO_OS_ERRNO, &os_errno) == CURLE_OK && os_errno != 0)
        error.os_error = SystemError{SystemError::Origin::Host, os_errno};
    return to_diagnostic(error);
}

[[nodiscard]] std::string ca_path_text(const NativePath& path) {
#ifdef _WIN32
    return display_utf8(path);
#else
    return path.string();
#endif
}

void finish(Transfer& transfer, Result<ports::HttpStatus> result) {
    UniqueFunction<void(Result<ports::HttpStatus>)> on_done = std::move(transfer.callbacks.on_done);
    transfer.callbacks = {};
    transfer.cancel.reset();
    transfer.wipe_request();
    if (!on_done) return;
    try {
        on_done(std::move(result));
    } catch (...) {
        REBOOT_LOG_ERROR(Net, "an HTTP completion callback threw");
    }
}

[[nodiscard]] Diagnostic cancelled(const Transfer& transfer) {
    return to_diagnostic(HttpError{.code = HttpErrorCode::Cancelled, .host = transfer.url.host});
}

}  // namespace

struct CurlHttpTransport::Impl {
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    ~Impl() {
        {
            const std::scoped_lock lock(mutex);
            stopping = true;
        }
        if (multi != nullptr) curl_multi_wakeup(multi);
        if (thread.joinable()) thread.join();
        if (multi != nullptr) curl_multi_cleanup(multi);
    }

    // Easy options are set on the caller's thread; the handle joins the multi on the transfer thread.
    Result<void> configure(Transfer& transfer) const {
        CURL* easy = transfer.easy;
        const ports::HttpRequest& request = transfer.request;
        const auto set = [&](CURLoption option, auto value) { return curl_easy_setopt(easy, option, value) == CURLE_OK; };
        bool ok = set(CURLOPT_URL, request.url.c_str()) && set(CURLOPT_PRIVATE, static_cast<void*>(&transfer)) &&
                  set(CURLOPT_ERRORBUFFER, transfer.error_buffer.data()) && set(CURLOPT_NOSIGNAL, 1L) &&
                  set(CURLOPT_PROTOCOLS_STR, "http,https") &&
                  set(CURLOPT_REDIR_PROTOCOLS_STR, transfer.url.scheme == UrlScheme::Https ? "https" : "http,https") &&
                  set(CURLOPT_FOLLOWLOCATION, 1L) && set(CURLOPT_MAXREDIRS, kMaxRedirects) &&
                  set(CURLOPT_SSL_VERIFYPEER, 1L) && set(CURLOPT_SSL_VERIFYHOST, 2L) &&
                  set(CURLOPT_USERAGENT, "RebootLauncher/" REBOOT_BUILD_ID) &&
                  set(CURLOPT_HEADERFUNCTION, &on_header_line) && set(CURLOPT_HEADERDATA, static_cast<void*>(&transfer)) &&
                  set(CURLOPT_WRITEFUNCTION, &on_body) && set(CURLOPT_WRITEDATA, static_cast<void*>(&transfer)) &&
                  set(CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(request.connect_timeout.count()));
#ifdef _WIN32
        // Schannel otherwise fails whenever a revocation server cannot be reached.
        ok = ok && set(CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_REVOKE_BEST_EFFORT));
#elif defined(__APPLE__)
        ok = ok && set(CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_NATIVE_CA));
#endif
        const std::optional<NativePath>& ca = request.tls.ca_bundle ? request.tls.ca_bundle : system_ca;
        if (ca) ok = ok && set(CURLOPT_CAINFO, ca_path_text(*ca).c_str());
        if (request.total_timeout > std::chrono::milliseconds::zero())
            ok = ok && set(CURLOPT_TIMEOUT_MS, static_cast<long>(request.total_timeout.count()));
        if (request.stall && request.stall->bytes_per_s > 0 && request.stall->window > std::chrono::milliseconds::zero()) {
            const long seconds = static_cast<long>((request.stall->window.count() + 999) / 1000);
            ok = ok && set(CURLOPT_LOW_SPEED_LIMIT, static_cast<long>(std::min<u64>(request.stall->bytes_per_s, 1u << 30))) &&
                 set(CURLOPT_LOW_SPEED_TIME, seconds);
        }

        if (request.method == "GET") {
            ok = ok && set(CURLOPT_HTTPGET, 1L);
        } else if (request.method == "HEAD") {
            ok = ok && set(CURLOPT_NOBODY, 1L);
        } else {
            static constexpr char kEmpty[] = "";
            const char* body = request.body.empty() ? kEmpty : reinterpret_cast<const char*>(request.body.data());
            ok = ok && set(CURLOPT_POSTFIELDS, body) &&
                 set(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
            if (request.method != "POST") ok = ok && set(CURLOPT_CUSTOMREQUEST, request.method.c_str());
        }

        curl_slist* list = nullptr;
        for (const ports::HttpHeader& header : request.headers) {
            std::string line = header.name + ": " + header.value;
            curl_slist* next = curl_slist_append(list, line.c_str());
            secure_wipe(line.data(), line.size());
            if (next == nullptr) {
                ok = false;
                break;
            }
            list = next;
        }
        transfer.header_list = list;
        if (list != nullptr) ok = ok && set(CURLOPT_HTTPHEADER, list);
        if (!ok) return std::unexpected(http_unavailable("curl refused a transfer option"));
        return {};
    }

    void run() {
        try {
            loop();
        } catch (...) {
            REBOOT_LOG_ERROR(Net, "the HTTP transfer thread failed");
            const std::scoped_lock lock(mutex);
            broken = true;
        }
        std::vector<TransferPtr> left;
        {
            const std::scoped_lock lock(mutex);
            for (TransferPtr& transfer : incoming) left.push_back(std::move(transfer));
            incoming.clear();
        }
        for (auto& [id, transfer] : active) {
            curl_multi_remove_handle(multi, transfer->easy);
            left.push_back(std::move(transfer));
        }
        active.clear();
        for (TransferPtr& transfer : left) finish(*transfer, std::unexpected(broken ? internal_bug("curl_http_transport") : cancelled(*transfer)));
    }

    void loop() {
        while (true) {
            std::vector<TransferPtr> added;
            std::vector<u64> cancels;
            {
                const std::scoped_lock lock(mutex);
                if (stopping) return;
                added.swap(incoming);
                cancels.swap(cancel_requests);
            }
            for (TransferPtr& transfer : added) {
                if (transfer->cancel_flag->load()) {
                    finish(*transfer, std::unexpected(cancelled(*transfer)));
                    continue;
                }
                if (curl_multi_add_handle(multi, transfer->easy) != CURLM_OK) {
                    finish(*transfer, std::unexpected(http_unavailable("curl refused a transfer")));
                    continue;
                }
                const u64 id = transfer->id;
                active.emplace(id, std::move(transfer));
            }
            for (const u64 id : cancels) {
                const auto it = active.find(id);
                if (it == active.end()) continue;
                TransferPtr transfer = std::move(it->second);
                active.erase(it);
                curl_multi_remove_handle(multi, transfer->easy);
                finish(*transfer, std::unexpected(cancelled(*transfer)));
            }

            int running = 0;
            curl_multi_perform(multi, &running);
            int queued = 0;
            while (CURLMsg* message = curl_multi_info_read(multi, &queued)) {
                if (message->msg != CURLMSG_DONE) continue;
                void* context = nullptr;
                curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &context);
                const CURLcode code = message->data.result;
                auto* raw = static_cast<Transfer*>(context);
                const auto it = active.find(raw->id);
                if (it == active.end()) continue;
                TransferPtr transfer = std::move(it->second);
                active.erase(it);
                curl_multi_remove_handle(multi, transfer->easy);
                if (code == CURLE_OK) {
                    try {
                        deliver_headers(*transfer);
                    } catch (...) {
                        transfer->callback_threw = true;
                    }
                    if (transfer->callback_threw) finish(*transfer, std::unexpected(internal_bug("curl_http_transport")));
                    else finish(*transfer, ports::HttpStatus{response_code(transfer->easy)});
                } else {
                    finish(*transfer, std::unexpected(transfer_error(*transfer, code)));
                }
            }
            curl_multi_poll(multi, nullptr, 0, kPollTimeoutMs, nullptr);
        }
    }

    CURLM* multi = nullptr;
    std::optional<NativePath> system_ca;
    std::thread thread;
    std::mutex mutex;
    bool stopping = false;
    bool broken = false;
    std::vector<TransferPtr> incoming;
    std::vector<u64> cancel_requests;
    std::atomic<u64> next_id{1};
    // Only the transfer thread touches it.
    std::map<u64, TransferPtr> active;
};

Result<std::unique_ptr<CurlHttpTransport>> CurlHttpTransport::create(const ports::ISystemInfo& system) {
    static const CURLcode global = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (global != CURLE_OK) return std::unexpected(http_unavailable(curl_easy_strerror(global)));
    auto impl = std::make_unique<Impl>();
    impl->multi = curl_multi_init();
    if (impl->multi == nullptr) return std::unexpected(http_unavailable("curl_multi_init failed"));
    impl->system_ca = system.ca_bundle();
    Impl* raw = impl.get();
    try {
        impl->thread = std::thread([raw] { raw->run(); });
    } catch (const std::system_error&) {
        return std::unexpected(http_unavailable("the transfer thread could not start"));
    }
    return std::unique_ptr<CurlHttpTransport>(new CurlHttpTransport(std::move(impl)));
}

CurlHttpTransport::CurlHttpTransport(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

CurlHttpTransport::~CurlHttpTransport() = default;

void CurlHttpTransport::perform(ports::HttpRequest request, ports::HttpCallbacks callbacks, CancelToken token) {
    auto transfer = std::make_unique<Transfer>();
    transfer->id = impl_->next_id.fetch_add(1);
    transfer->request = std::move(request);
    transfer->callbacks = std::move(callbacks);
    const std::optional<ParsedUrl> url = parse_url(transfer->request.url);
    if (!url) {
        finish(*transfer, std::unexpected(to_diagnostic(HttpError{.code = HttpErrorCode::InvalidUrl, .host = transfer->request.url})));
        return;
    }
    transfer->url = *url;
    transfer->easy = curl_easy_init();
    if (transfer->easy == nullptr) {
        finish(*transfer, std::unexpected(http_unavailable("curl_easy_init failed")));
        return;
    }
    if (Result<void> configured = impl_->configure(*transfer); !configured) {
        finish(*transfer, std::unexpected(std::move(configured.error())));
        return;
    }

    Impl* impl = impl_.get();
    const u64 id = transfer->id;
    // The registration lives in the transfer, so it is reset before the transfer is gone.
    transfer->cancel = token.on_cancel([impl, id, flag = transfer->cancel_flag](CancelReason) {
        flag->store(true);
        {
            const std::scoped_lock lock(impl->mutex);
            impl->cancel_requests.push_back(id);
        }
        curl_multi_wakeup(impl->multi);
    });
    bool broken = false;
    {
        const std::scoped_lock lock(impl->mutex);
        broken = impl->broken;
        if (!impl->stopping && !broken) {
            impl->incoming.push_back(std::move(transfer));
            transfer = nullptr;
        }
    }
    if (transfer) {
        finish(*transfer, std::unexpected(broken ? internal_bug("curl_http_transport") : cancelled(*transfer)));
        return;
    }
    curl_multi_wakeup(impl->multi);
}

}  // namespace rb::net
