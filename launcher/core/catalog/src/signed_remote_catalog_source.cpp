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

SignedRemoteCatalogSource::~SignedRemoteCatalogSource() = default;

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
        [this, missing]() -> std::expected<CachedCopy, CatalogError> {
            const auto absent = [&](const NativePath& path, Diagnostic cause) {
                CatalogError error = missing;
                error.path = path;
                error.cause = std::move(cause);
                return std::unexpected(std::move(error));
            };
            auto body = files_.read_all(cache_body_);
            if (!body) return absent(cache_body_, std::move(body.error()));
            auto signature = files_.read_all(cache_signature_);
            if (!signature) return absent(cache_signature_, std::move(signature.error()));
            auto catalog = verify_and_parse(keys_, std::move(*body), as_text(*signature));
            if (!catalog) return std::unexpected(std::move(catalog.error()));
            // A missing or unreadable ETag only costs a full download.
            auto etag = files_.read_all(cache_etag_);
            return CachedCopy{.catalog = std::move(*catalog), .etag = etag ? std::string(as_text(*etag)) : std::string()};
        },
        missing, [this](std::expected<CachedCopy, CatalogError> cached) { on_cache_read(std::move(cached)); });
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

    auto sent = http_.send(std::move(request), load_->token, [this](Result<net::HttpResponse> response) {
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
        load_->etag = etag != nullptr ? *etag : std::string();
        load_->body = std::move(response->body);
        fetch_signature();
    });
    if (!sent)
        finish(std::unexpected(
            CatalogError{.code = CatalogErrorCode::FetchFailed, .url = location_.catalog_url, .cause = std::move(sent.error())}));
}

void SignedRemoteCatalogSource::fetch_signature() {
    const net::HttpRequest request{.url = location_.signature_url, .max_body = kSignatureMaxBody};
    auto sent = http_.send(request, load_->token, [this](Result<net::HttpResponse> response) {
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
            [this, body = load_->body, signature = load_->signature]() mutable -> CatalogLoadResult {
                auto catalog = verify_and_parse(keys_, std::move(body), as_text(signature));
                if (!catalog) return std::unexpected(std::move(catalog.error()));
                return LoadedCatalog{.catalog = std::move(*catalog), .origin = CatalogOrigin::Remote, .warnings = {}};
            },
            CatalogError{.code = CatalogErrorCode::Untrusted},
            [this](CatalogLoadResult fetched) { on_verified(std::move(fetched)); });
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
        [this, write_failed, writes = std::move(writes)]() -> std::expected<void, CatalogError> {
            const auto fail = [&](const NativePath& path, Diagnostic cause) {
                CatalogError error = write_failed;
                error.path = path;
                error.cause = std::move(cause);
                return std::unexpected(std::move(error));
            };
            const NativePath dir = cache_body_.parent_path();
            if (auto created = files_.create_dirs_owner_only(dir); !created) return fail(dir, std::move(created.error()));
            // A crash between writes leaves a body and .sig that fail to verify, so the next load refetches.
            for (const auto& write : writes)
                if (auto written = files_.atomic_replace(write.path, write.bytes, false); !written)
                    return fail(write.path, std::move(written.error()));
            return {};
        },
        write_failed,
        [this, fetched = std::move(*fetched)](std::expected<void, CatalogError> written) mutable {
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
