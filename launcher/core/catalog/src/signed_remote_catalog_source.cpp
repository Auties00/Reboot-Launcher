#include "reboot/catalog/signed_remote_catalog_source.hpp"

#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/net/http_request.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/trust/serial_guard.hpp"
#include "verified_catalog.hpp"

namespace reboot::catalog {

namespace {

// The ".sig" line is about 160 bytes.
constexpr std::size_t kSignatureMaxBody = 4096;
constexpr std::size_t kEtagMaxSize = 1024;

[[nodiscard]] NativePath with_suffix(const NativePath& path, std::string_view suffix) {
    NativePath out = path;
    out += suffix;
    return out;
}

[[nodiscard]] std::string_view as_text(std::span<const u8> bytes) noexcept {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] std::span<const u8> as_bytes(std::string_view text) noexcept {
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}

// Visible ASCII only, so an edited or hostile .etag file can never inject a header line.
[[nodiscard]] std::string usable_etag(std::string_view etag) {
    if (etag.size() > kEtagMaxSize) return {};
    for (const char c : etag)
        if (c < '!' || c > '~') return {};
    return std::string(etag);
}

}  // namespace

struct SignedRemoteCatalogSource::CachedCopy {
    Catalog catalog;
    // Empty when no ETag was stored.
    std::string etag;
};

struct SignedRemoteCatalogSource::Load {
    CatalogFetch fetch = CatalogFetch::CacheOnly;
    CancelToken token;
    UniqueFunction<void(CatalogLoadResult)> done;
    std::optional<CachedCopy> cached;
    std::vector<u8> body;
    std::vector<u8> signature;
    std::string etag;
};

SignedRemoteCatalogSource::SignedRemoteCatalogSource(RemoteCatalogLocation location, const AppLayout& layout,
                                                     net::HttpClient& http, ports::IFileSystem& files,
                                                     WorkerPool& workers, Executor& strand,
                                                     const trust::KeyRing& keys, trust::SerialGuard& serials)
    : location_(std::move(location)),
      cache_body_(layout.catalog_cache()),
      cache_signature_(with_suffix(cache_body_, ".sig")),
      cache_etag_(with_suffix(cache_body_, ".etag")),
      http_(http),
      files_(files),
      workers_(workers),
      strand_(strand),
      keys_(keys),
      serials_(serials) {}

SignedRemoteCatalogSource::~SignedRemoteCatalogSource() { alive_.cancel(CancelReason::Shutdown); }

void SignedRemoteCatalogSource::load(CatalogFetch fetch, CancelToken token,
                                     UniqueFunction<void(CatalogLoadResult)> done) {
    if (load_) {
        done(std::unexpected(CatalogError{.code = CatalogErrorCode::FetchFailed,
                                          .url = location_.catalog_url,
                                          .cause = internal_bug("catalog: overlapping remote load")}));
        return;
    }
    load_ = std::make_unique<Load>(Load{.fetch = fetch, .token = token, .done = std::move(done)});

    const CatalogError missing{.code = CatalogErrorCode::CacheMissing, .path = cache_body_};
    submit_catalog_work<CachedCopy>(
        workers_, strand_, std::move(token),
        [&files = files_, &keys = keys_, body_path = cache_body_, signature_path = cache_signature_,
         etag_path = cache_etag_, missing]() -> std::expected<CachedCopy, CatalogError> {
            const auto absent = [&](const NativePath& path, Diagnostic cause) {
                CatalogError error = missing;
                error.path = path;
                error.cause = std::move(cause);
                return std::unexpected(std::move(error));
            };
            auto body = files.read_all(body_path);
            if (!body) return absent(body_path, std::move(body.error()));
            auto signature = files.read_all(signature_path);
            if (!signature) return absent(signature_path, std::move(signature.error()));
            auto catalog = verify_and_parse(keys, std::move(*body), as_text(*signature));
            if (!catalog) return std::unexpected(std::move(catalog.error()));
            // A missing or unreadable ETag only costs a full download.
            auto etag = files.read_all(etag_path);
            return CachedCopy{.catalog = std::move(*catalog), .etag = etag ? usable_etag(as_text(*etag)) : std::string()};
        },
        missing, [this, alive = alive_.token()](std::expected<CachedCopy, CatalogError> cached) {
            if (!alive.cancelled()) on_cache_read(std::move(cached));
        });
}

void SignedRemoteCatalogSource::on_cache_read(std::expected<CachedCopy, CatalogError> cached) {
    if (cached) {
        // The cached copy was admitted when written; a lower serial means it was swapped since.
        if (auto admitted = serials_.admit(cached->catalog.serial); !admitted)
            cached = std::unexpected(
                CatalogError{.code = CatalogErrorCode::Untrusted, .cause = trust::to_diagnostic(admitted.error())});
    }
    if (load_->fetch == CatalogFetch::CacheOnly) {
        if (!cached) return finish(std::unexpected(std::move(cached.error())));
        return finish(LoadedCatalog{.catalog = std::move(cached->catalog), .origin = CatalogOrigin::Cache, .warnings = {}});
    }
    if (cached) load_->cached = std::move(*cached);
    fetch_body();
}

void SignedRemoteCatalogSource::fetch_body() {
    net::HttpRequest request{.url = location_.catalog_url};
    if (load_->cached && !load_->cached->etag.empty())
        request.headers.push_back({.name = "If-None-Match", .value = load_->cached->etag});

    auto sent = http_.send(std::move(request), load_->token, [this, alive = alive_.token()](Result<net::HttpResponse> response) {
        if (alive.cancelled()) return;
        if (!response)
            return finish(std::unexpected(CatalogError{
                .code = CatalogErrorCode::FetchFailed, .url = location_.catalog_url, .cause = std::move(response.error())}));
        if (response->status == 304 && load_->cached)
            return finish(LoadedCatalog{
                .catalog = std::move(load_->cached->catalog), .origin = CatalogOrigin::Cache, .warnings = {}});
        if (response->status != 200)
            return finish(std::unexpected(CatalogError{
                .code = CatalogErrorCode::HttpStatus, .url = location_.catalog_url, .http_status = response->status}));
        const std::string* etag = response->header("ETag");
        load_->etag = etag != nullptr ? usable_etag(*etag) : std::string();
        load_->body = std::move(response->body);
        fetch_signature();
    });
    if (!sent)
        finish(std::unexpected(
            CatalogError{.code = CatalogErrorCode::FetchFailed, .url = location_.catalog_url, .cause = std::move(sent.error())}));
}

void SignedRemoteCatalogSource::fetch_signature() {
    const net::HttpRequest request{.url = location_.signature_url, .max_body = kSignatureMaxBody};
    auto sent = http_.send(request, load_->token, [this, alive = alive_.token()](Result<net::HttpResponse> response) {
        if (alive.cancelled()) return;
        if (!response)
            return finish(std::unexpected(CatalogError{.code = CatalogErrorCode::FetchFailed,
                                                       .url = location_.signature_url,
                                                       .cause = std::move(response.error())}));
        if (response->status != 200)
            return finish(std::unexpected(CatalogError{
                .code = CatalogErrorCode::HttpStatus, .url = location_.signature_url, .http_status = response->status}));

        load_->signature = std::move(response->body);
        submit_catalog_work<LoadedCatalog>(
            workers_, strand_, load_->token,
            [&keys = keys_, body = load_->body, signature = load_->signature]() mutable -> CatalogLoadResult {
                auto catalog = verify_and_parse(keys, std::move(body), as_text(signature));
                if (!catalog) return std::unexpected(std::move(catalog.error()));
                return LoadedCatalog{.catalog = std::move(*catalog), .origin = CatalogOrigin::Remote, .warnings = {}};
            },
            CatalogError{.code = CatalogErrorCode::Untrusted},
            [this, alive](CatalogLoadResult fetched) {
                if (!alive.cancelled()) on_verified(std::move(fetched));
            });
    });
    if (!sent)
        finish(std::unexpected(CatalogError{
            .code = CatalogErrorCode::FetchFailed, .url = location_.signature_url, .cause = std::move(sent.error())}));
}

void SignedRemoteCatalogSource::on_verified(CatalogLoadResult fetched) {
    if (!fetched) return finish(std::move(fetched));
    if (auto admitted = serials_.admit(fetched->catalog.serial); !admitted)
        return finish(std::unexpected(
            CatalogError{.code = CatalogErrorCode::Untrusted, .cause = trust::to_diagnostic(admitted.error())}));

    struct Write {
        NativePath path;
        std::vector<u8> bytes;
    };
    const auto etag = as_bytes(load_->etag);
    std::vector<Write> writes;
    writes.push_back({cache_body_, std::move(load_->body)});
    writes.push_back({cache_signature_, std::move(load_->signature)});
    writes.push_back({cache_etag_, std::vector<u8>(etag.begin(), etag.end())});

    const CatalogError write_failed{.code = CatalogErrorCode::CacheWriteFailed, .path = cache_body_};
    submit_catalog_work<void>(
        workers_, strand_, load_->token,
        [&files = files_, dir = cache_body_.parent_path(), write_failed,
         writes = std::move(writes)]() -> std::expected<void, CatalogError> {
            const auto fail = [&](const NativePath& path, Diagnostic cause) {
                CatalogError error = write_failed;
                error.path = path;
                error.cause = std::move(cause);
                return std::unexpected(std::move(error));
            };
            if (auto created = files.create_dirs_owner_only(dir); !created) return fail(dir, std::move(created.error()));
            // A crash between writes leaves a body and .sig that fail to verify, so the next load refetches.
            for (const auto& write : writes)
                if (auto written = files.atomic_replace(write.path, write.bytes, false); !written)
                    return fail(write.path, std::move(written.error()));
            return {};
        },
        write_failed,
        [this, alive = alive_.token(), fetched = std::move(*fetched)](std::expected<void, CatalogError> written) mutable {
            if (alive.cancelled()) return;
            if (!written) {
                Diagnostic warning = to_diagnostic(written.error());
                warning.severity = Severity::Warning;
                fetched.warnings.push_back(std::move(warning));
            }
            finish(std::move(fetched));
        });
}

void SignedRemoteCatalogSource::finish(CatalogLoadResult result) {
    const std::unique_ptr<Load> load = std::move(load_);
    load->done(std::move(result));
}

}  // namespace reboot::catalog
