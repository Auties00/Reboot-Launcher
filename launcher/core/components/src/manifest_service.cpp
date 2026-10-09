#include "reboot/components/manifest_service.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <span>
#include <utility>

#include "messages.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/net/http_request.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/trust/check_expiry.hpp"
#include "reboot/trust/key_ring.hpp"
#include "reboot/trust/serial_guard.hpp"
#include "reboot/trust/signed_document.hpp"
#include "reboot/trust/trust_error.hpp"
#include "reboot/trust/verify_signed.hpp"

namespace rb::components {

namespace {

// The ".sig" line is about 160 bytes.
constexpr std::size_t kSignatureMaxBody = 4096;

[[nodiscard]] NativePath with_suffix(const NativePath& path, std::string_view suffix) {
    NativePath out = path;
    out += suffix;
    return out;
}

[[nodiscard]] std::string_view as_text(std::span<const u8> bytes) noexcept {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] Diagnostic components_diag(MessageId message) {
    return make_diag(ErrorDomain::Components, message).build();
}

// UTC, to the second: "2026-10-08T12:00:00Z".
[[nodiscard]] std::string format_utc(std::chrono::system_clock::time_point time) {
    using namespace std::chrono;
    const sys_seconds seconds = floor<std::chrono::seconds>(time);
    const sys_days day = floor<days>(seconds);
    const year_month_day date(day);
    const hh_mm_ss<std::chrono::seconds> clock_time(seconds - day);
    char text[32];
    std::snprintf(text, sizeof text, "%04d-%02u-%02uT%02d:%02d:%02dZ", static_cast<int>(date.year()),
                  static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()),
                  static_cast<int>(clock_time.hours().count()), static_cast<int>(clock_time.minutes().count()),
                  static_cast<int>(clock_time.seconds().count()));
    return text;
}

struct Fetched {
    ReleaseManifest manifest;
    std::vector<u8> body;
    std::vector<u8> signature;
};

[[nodiscard]] Result<ReleaseManifest> verify_and_parse(const trust::KeyRing& keys, std::vector<u8> body,
                                                       std::string_view signature_file) {
    auto document =
        trust::make_signed_document(trust::SignedDocumentKind::ReleaseManifest, std::move(body), signature_file);
    if (!document) return std::unexpected(trust::to_diagnostic(document.error()));
    if (auto verified = trust::verify_signed(keys, *document); !verified)
        return std::unexpected(trust::to_diagnostic(verified.error()));
    return parse_release_manifest(document->body);
}

[[nodiscard]] Result<ReleaseManifest> read_verified(ports::IFileSystem& fs, const trust::KeyRing& keys,
                                                    const NativePath& body_path, const NativePath& signature_path) {
    auto body = fs.read_all(body_path);
    if (!body) return std::unexpected(std::move(body.error()));
    auto signature = fs.read_all(signature_path);
    if (!signature) return std::unexpected(std::move(signature.error()));
    return verify_and_parse(keys, std::move(*body), as_text(*signature));
}

[[nodiscard]] Diagnostic fetch_failed(const std::string& url, std::optional<u32> status, std::optional<Diagnostic> cause) {
    DiagBuilder builder = make_diag(ErrorDomain::Components, kManifestFetchFailed).arg("url", url).retryable();
    if (status) std::move(builder).arg("status", *status).retryable(*status == 429 || *status >= 500);
    if (cause) std::move(builder).cause(std::move(*cause));
    return std::move(builder).build();
}

}  // namespace

struct ManifestService::Impl {
    struct Startup {
        Result<ReleaseManifest> cached;
        Result<ReleaseManifest> bundled;
    };

    Impl(ManifestServiceDeps service_deps, ManifestOptions manifest_options, const AppLayout& layout,
         const InstallLayout& install)
        : deps(service_deps),
          options(std::move(manifest_options)),
          cache_body(layout.manifest_cache()),
          cache_signature(with_suffix(cache_body, ".sig")),
          bundled_body(install.bundled_manifest),
          bundled_signature(with_suffix(bundled_body, ".sig")) {}

    void on_startup_read(Startup read, UniqueFunction<void(Result<ManifestOrigin>)> done) {
        // A refresh that finished first already adopted something newer.
        if (current) return done(*origin);
        if (read.cached) {
            if (auto admitted = admit_cached(*read.cached); !admitted) read.cached = std::unexpected(std::move(admitted.error()));
        }
        // An app update can ship a bundled snapshot newer than the cache.
        if (read.cached && (!read.bundled || read.cached->serial >= read.bundled->serial)) {
            adopt_quietly(std::move(*read.cached), ManifestOrigin::Cached);
            return done(ManifestOrigin::Cached);
        }
        if (!read.cached && read.cached.error().kind != ErrorKind::NotFound)
            REBOOT_LOG_WARN(Update, "The cached release manifest is unusable ({}); using the bundled one",
                            read.cached.error().id);
        if (read.bundled) {
            adopt_quietly(std::move(*read.bundled), ManifestOrigin::Bundled);
            return done(ManifestOrigin::Bundled);
        }
        done(make_diag(ErrorDomain::Components, kManifestUnavailable)
                 .cause(std::move(read.cached.error()))
                 .cause(std::move(read.bundled.error()))
                 .fail());
    }

    // The cache was admitted when written; a lower serial means it was swapped since. An expired copy
    // is still used but never raises the floor.
    Result<void> admit_cached(const ReleaseManifest& manifest) {
        const u64 highest = deps.serials.highest_seen();
        if (manifest.serial < highest)
            return std::unexpected(trust::to_diagnostic(trust::TrustError{.code = trust::TrustErrorCode::SerialRollback,
                                                                          .document = trust::SignedDocumentKind::ReleaseManifest,
                                                                          .serial = manifest.serial,
                                                                          .highest_seen = highest}));
        if (deps.clock.system_now() > manifest.expires_at) return {};
        if (auto admitted = deps.serials.admit(manifest.serial); !admitted)
            return std::unexpected(trust::to_diagnostic(admitted.error()));
        return {};
    }

    void adopt_quietly(ReleaseManifest manifest, ManifestOrigin from) {
        current = std::move(manifest);
        origin = from;
    }

    struct Waiter {
        u64 id = 0;
        UniqueFunction<void(Result<ManifestRefresh>)> done;
        CancelRegistration registration;
    };

    void join(CancelToken token, UniqueFunction<void(Result<ManifestRefresh>)> done) {
        const u64 id = next_waiter++;
        waiters.push_back(Waiter{.id = id, .done = std::move(done), .registration = {}});
        waiters.back().registration = token.on_cancel([this, id](CancelReason) {
            deps.strand.post([this, id] { drop_waiter(id); });
        });
        if (in_flight) return;
        in_flight = true;
        cancel = CancelSource{};
        start_refresh(cancel.token());
    }

    // A cancelled caller stops only its own wait; the last one stops the refresh.
    void drop_waiter(u64 id) {
        const auto waiter = std::ranges::find(waiters, id, &Waiter::id);
        if (waiter == waiters.end()) return;
        UniqueFunction<void(Result<ManifestRefresh>)> done = std::move(waiter->done);
        waiters.erase(waiter);
        if (waiters.empty()) cancel.cancel(CancelReason::User);
        done(std::unexpected(make_diag(ErrorDomain::Components, kManifestFetchFailed)
                                 .arg("url", options.url)
                                 .kind(ErrorKind::Cancelled)
                                 .build()));
    }

    void start_refresh(CancelToken token) {
        net::HttpRequest request{.url = options.url};
        auto sent = deps.http.send(std::move(request), token, [this, token](Result<net::HttpResponse> response) {
            if (!response) return fail(fetch_failed(options.url, std::nullopt, std::move(response.error())));
            if (response->status != 200) return fail(fetch_failed(options.url, response->status, std::nullopt));
            fetch_signature(std::move(response->body), token);
        });
        if (!sent) fail(fetch_failed(options.url, std::nullopt, std::move(sent.error())));
    }

    void fetch_signature(std::vector<u8> body, CancelToken token) {
        net::HttpRequest request{.url = options.signature_url, .max_body = kSignatureMaxBody};
        auto sent = deps.http.send(
            std::move(request), token, [this, token, body = std::move(body)](Result<net::HttpResponse> response) mutable {
                if (!response)
                    return fail(fetch_failed(options.signature_url, std::nullopt, std::move(response.error())));
                if (response->status != 200)
                    return fail(fetch_failed(options.signature_url, response->status, std::nullopt));
                verify(std::move(body), std::move(response->body), token);
            });
        if (!sent) fail(fetch_failed(options.signature_url, std::nullopt, std::move(sent.error())));
    }

    void verify(std::vector<u8> body, std::vector<u8> signature, CancelToken token) {
        deps.workers.submit<Fetched>(
            [keys = &deps.keys, body = std::move(body), signature = std::move(signature)](CancelToken) mutable
                -> Result<Fetched> {
                auto manifest = verify_and_parse(*keys, body, as_text(signature));
                if (!manifest) return std::unexpected(std::move(manifest.error()));
                return Fetched{std::move(*manifest), std::move(body), std::move(signature)};
            },
            std::move(token), deps.strand, [this](Result<Fetched> fetched) {
                if (!fetched) return fail(std::move(fetched.error()));
                gate(std::move(*fetched));
            });
    }

    // Signature, then expiry, then serial: an expired manifest must never raise the floor.
    void gate(Fetched fetched) {
        const ReleaseManifest& manifest = fetched.manifest;
        if (deps.clock.system_now() > manifest.expires_at)
            return fail(make_diag(ErrorDomain::Components, kManifestExpired)
                              .arg("expires_at", format_utc(manifest.expires_at))
                              .build());
        auto admitted = deps.serials.admit(manifest.serial);
        if (!admitted) return fail(trust::to_diagnostic(admitted.error()));
        if (*admitted == trust::SerialCheck::Same && current && current->serial == manifest.serial)
            return finish(ManifestRefresh::Unchanged);
        write_cache(std::move(fetched));
    }

    void write_cache(Fetched fetched) {
        std::vector<u8> body = std::move(fetched.body);
        std::vector<u8> signature = std::move(fetched.signature);
        deps.workers.submit<void>(
            [this, body = std::move(body), signature = std::move(signature)](CancelToken) -> Result<void> {
                if (auto created = deps.fs.create_dirs_owner_only(cache_body.parent_path()); !created) return created;
                // A crash between the writes leaves a pair that fails to verify, so the next start uses the bundled copy.
                if (auto written = deps.fs.atomic_replace(cache_body, body, false); !written) return written;
                return deps.fs.atomic_replace(cache_signature, signature, false);
            },
            CancelToken{}, deps.strand, [this, manifest = std::move(fetched.manifest)](Result<void> written) mutable {
                if (!written)
                    REBOOT_LOG_WARN(Update, "Cannot cache the release manifest: {}", written.error().id);
                current = std::move(manifest);
                origin = ManifestOrigin::Fetched;
                // By index: a listener may add another.
                for (std::size_t i = 0; i < listeners.size(); ++i) listeners[i](*current);
                finish(ManifestRefresh::Adopted);
            });
    }

    void fail(Diagnostic error) { finish(std::unexpected(std::move(error))); }

    void finish(Result<ManifestRefresh> result) {
        // Callers that joined after every earlier one gave up get a fresh refresh.
        if (!result && cancel.cancelled() && !waiters.empty()) {
            cancel = CancelSource{};
            return start_refresh(cancel.token());
        }
        in_flight = false;
        std::vector<Waiter> done = std::move(waiters);
        waiters.clear();
        for (Waiter& waiter : done) {
            waiter.registration.reset();
            waiter.done(result);
        }
    }

    ManifestServiceDeps deps;
    ManifestOptions options;
    NativePath cache_body;
    NativePath cache_signature;
    NativePath bundled_body;
    NativePath bundled_signature;
    std::optional<ReleaseManifest> current;
    std::optional<ManifestOrigin> origin;
    std::vector<UniqueFunction<void(const ReleaseManifest&)>> listeners;
    bool in_flight = false;
    CancelSource cancel;
    std::vector<Waiter> waiters;
    u64 next_waiter = 1;
};

ManifestService::ManifestService(ManifestServiceDeps deps, ManifestOptions options, const AppLayout& layout,
                                 const InstallLayout& install)
    : impl_(std::make_unique<Impl>(deps, std::move(options), layout, install)) {}

ManifestService::~ManifestService() = default;

void ManifestService::load(UniqueFunction<void(Result<ManifestOrigin>)> done) {
    Impl* impl = impl_.get();
    impl->deps.workers.submit<Impl::Startup>(
        [impl](CancelToken) -> Result<Impl::Startup> {
            return Impl::Startup{
                .cached = read_verified(impl->deps.fs, impl->deps.keys, impl->cache_body, impl->cache_signature),
                .bundled = read_verified(impl->deps.fs, impl->deps.keys, impl->bundled_body, impl->bundled_signature)};
        },
        CancelToken{}, impl->deps.strand, [impl, done = std::move(done)](Result<Impl::Startup> read) mutable {
            if (!read) return done(std::unexpected(std::move(read.error())));
            impl->on_startup_read(std::move(*read), std::move(done));
        });
}

void ManifestService::refresh(CancelToken token, UniqueFunction<void(Result<ManifestRefresh>)> done) {
    impl_->join(std::move(token), std::move(done));
}

const ReleaseManifest* ManifestService::current() const noexcept {
    return impl_->current ? &*impl_->current : nullptr;
}

std::optional<ManifestOrigin> ManifestService::origin() const noexcept { return impl_->origin; }

ManifestPlatform ManifestService::platform() const noexcept { return impl_->options.platform; }

std::optional<Diagnostic> ManifestService::expiry_warning() const {
    if (!impl_->current) return std::nullopt;
    return trust::check_expiry(trust::SignedDocumentKind::ReleaseManifest, impl_->current->expires_at,
                               impl_->deps.clock.system_now());
}

std::optional<AppEntry> ManifestService::app_entry() const {
    if (!impl_->current) return std::nullopt;
    for (const AppEntry& entry : impl_->current->apps)
        if (entry.platform == impl_->options.platform && entry.channel == impl_->options.channel) return entry;
    return std::nullopt;
}

std::optional<AppEntry> ManifestService::update_offer(const SemVer& installed) const {
    std::optional<AppEntry> entry = app_entry();
    if (!entry || !offers_update(*entry, installed)) return std::nullopt;
    return entry;
}

std::optional<EndpointOverride> ManifestService::endpoint_override() const {
    if (!impl_->current) return std::nullopt;
    return impl_->current->endpoint;
}

Result<PayloadEntry> ManifestService::payload() const {
    if (!impl_->current) return std::unexpected(components_diag(kManifestUnavailable));
    const std::span<const PayloadRole> roles = required_payload_roles(impl_->options.platform.os);
    const PayloadEntry* best = nullptr;
    for (const PayloadEntry& entry : impl_->current->payloads) {
        if (entry.payload_abi != VersionStreams::payload_abi) continue;
        bool complete = true;
        for (const PayloadRole role : roles) complete = complete && entry.find(role) != nullptr;
        if (complete && (best == nullptr || entry.version > best->version)) best = &entry;
    }
    if (best == nullptr)
        return make_diag(ErrorDomain::Components, kNoPayload)
            .arg("payload_abi", VersionStreams::payload_abi)
            .kind(ErrorKind::NotFound)
            .fail();
    return *best;
}

Result<RuntimeEntry> ManifestService::runtime(std::string_view id) const {
    if (!impl_->current) return std::unexpected(components_diag(kManifestUnavailable));
    for (const RuntimeEntry& entry : impl_->current->runtimes) {
        if (entry.id != id) continue;
        if (!runtime_matches(entry, impl_->options.platform))
            return make_diag(ErrorDomain::Components, kRuntimeWrongPlatform)
                .arg("runtime_id", id)
                .kind(ErrorKind::Unsupported)
                .fail();
        return entry;
    }
    return make_diag(ErrorDomain::Components, kUnknownRuntime).arg("runtime_id", id).kind(ErrorKind::NotFound).fail();
}

std::vector<RuntimeEntry> ManifestService::runtimes() const {
    std::vector<RuntimeEntry> out;
    if (!impl_->current) return out;
    for (const RuntimeEntry& entry : impl_->current->runtimes)
        if (runtime_matches(entry, impl_->options.platform)) out.push_back(entry);
    return out;
}

void ManifestService::add_listener(UniqueFunction<void(const ReleaseManifest&)> on_changed) {
    impl_->listeners.push_back(std::move(on_changed));
}

}  // namespace rb::components
