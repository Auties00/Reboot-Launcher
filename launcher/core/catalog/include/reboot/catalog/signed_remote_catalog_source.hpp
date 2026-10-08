#pragma once

#include <expected>
#include <memory>
#include <string>

#include "reboot/catalog/catalog_source.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"

namespace reboot {
class AppLayout;
class Executor;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class IFileSystem;
}

namespace reboot::net {
class HttpClient;
}

namespace reboot::trust {
class KeyRing;
class SerialGuard;
}  // namespace reboot::trust

namespace reboot::catalog {

struct RemoteCatalogLocation {
    std::string catalog_url;
    // The detached "ed25519 <key_id> <signature>" file for the body at catalog_url.
    std::string signature_url;
};

// game-builds.catalog, game-builds.+0, game-builds.historical-remote-catalog: the Ed25519-signed
// catalog fetched over HTTPS, so a mirror rename ships as data instead of a launcher release.
// It caches cache/catalog.json with catalog.json.sig and catalog.json.etag beside it.
class SignedRemoteCatalogSource final : public ICatalogSource {
public:
    SignedRemoteCatalogSource(RemoteCatalogLocation location, const AppLayout& layout, net::HttpClient& http,
                              ports::IFileSystem& files, WorkerPool& workers, Executor& strand,
                              const trust::KeyRing& keys, trust::SerialGuard& serials);
    ~SignedRemoteCatalogSource() override;
    SignedRemoteCatalogSource(const SignedRemoteCatalogSource&) = delete;
    SignedRemoteCatalogSource& operator=(const SignedRemoteCatalogSource&) = delete;

    // CacheOnly re-verifies the cached copy and its serial. Revalidate sends If-None-Match only
    // while that copy verifies; a 304 yields it as Cache, and a 200 must verify, parse at
    // kCatalogSchema and pass the serial guard before it replaces the cache. A rejected body never
    // touches the cache.
    void load(CatalogFetch fetch, CancelToken token, UniqueFunction<void(CatalogLoadResult)> done) override;

private:
    struct Load;
    struct CachedCopy;

    void on_cache_read(std::expected<CachedCopy, CatalogError> cached);
    void fetch_body();
    void fetch_signature();
    void on_verified(CatalogLoadResult fetched);
    void finish(CatalogLoadResult result);

    RemoteCatalogLocation location_;
    NativePath cache_body_;
    NativePath cache_signature_;
    NativePath cache_etag_;
    net::HttpClient& http_;
    ports::IFileSystem& files_;
    WorkerPool& workers_;
    Executor& strand_;
    const trust::KeyRing& keys_;
    trust::SerialGuard& serials_;
    // The load in flight; ICatalogSource callers never overlap loads.
    std::unique_ptr<Load> load_;
};

}  // namespace reboot::catalog
