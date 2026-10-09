#include "reboot/catalog/catalog_service.hpp"

#include <initializer_list>
#include <utility>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/trust/check_expiry.hpp"
#include "reboot/trust/trust_error.hpp"

namespace rb::catalog {

namespace {

[[nodiscard]] Diagnostic as_warning(const CatalogError& error) {
    Diagnostic warning = to_diagnostic(error);
    warning.severity = Severity::Warning;
    return warning;
}

[[nodiscard]] Diagnostic rollback_warning(u64 serial, u64 active) {
    return as_warning(CatalogError{
        .code = CatalogErrorCode::Untrusted,
        .cause = trust::to_diagnostic(trust::TrustError{.code = trust::TrustErrorCode::SerialRollback,
                                                        .document = trust::SignedDocumentKind::BuildCatalog,
                                                        .serial = serial,
                                                        .highest_seen = active})});
}

// A first run has no cache; any other cache failure is worth a warning.
[[nodiscard]] bool no_cache_yet(const CatalogError& error) noexcept {
    return error.code == CatalogErrorCode::CacheMissing && (!error.cause || error.cause->kind == ErrorKind::NotFound);
}

// Its op already has an outcome; completing it only lets the registry free it.
void release(Operation<CatalogUpdated>& op) { (void)op.complete(Cancelled{}); }

}  // namespace

CatalogService::CatalogService(ICatalogSource& remote, ICatalogSource& bundled, OpRegistry& ops, EventBus& events,
                               const IClock& clock)
    : remote_(remote), bundled_(bundled), ops_(ops), events_(events), clock_(clock) {}

CatalogService::~CatalogService() {
    alive_.cancel(CancelReason::Shutdown);
    for (std::optional<Refresh>* refresh : {&running_, &queued_}) {
        if (!*refresh) continue;
        (void)ops_.cancel((*refresh)->handle.id(), CancelReason::Shutdown);
        release(*(*refresh)->op);
    }
}

std::vector<CatalogEntry> CatalogService::list(const CatalogFilter& filter) const {
    std::vector<CatalogEntry> out;
    for (const auto& entry : current_.entries)
        if (filter.include_unavailable || entry.installable()) out.push_back(entry);
    return out;
}

Result<CatalogEntry> CatalogService::entry(std::string_view name) const {
    const auto not_found = [&] {
        return std::unexpected(
            to_diagnostic(CatalogError{.code = CatalogErrorCode::EntryNotFound, .entry = CatalogEntryId(name)}));
    };
    const auto match = aliases_.resolve(name);
    if (!match) return not_found();
    const CatalogEntry* found = current_.find(match->entry);
    if (found == nullptr) return not_found();
    return *found;
}

Result<CatalogEntry> CatalogService::installable_entry(std::string_view name) const {
    auto found = entry(name);
    if (found && !found->installable())
        return std::unexpected(
            to_diagnostic(CatalogError{.code = CatalogErrorCode::EntryNotInstallable, .entry = found->id}));
    return found;
}

Result<OpHandle> CatalogService::start_refresh(CatalogRefresh mode, DisconnectPolicy policy) {
    if (queued_ && !queued_->op->done()) return queued_->handle;
    if (running_ && !running_->op->done()) return running_->handle;
    if (queued_) {
        release(*queued_->op);
        queued_.reset();
    }

    auto [handle, op] = ops_.create<CatalogUpdated>(OpKind::HttpSmall, policy, std::nullopt);
    Refresh refresh{.handle = handle, .op = &op, .mode = mode, .warnings = {}, .changed = false};
    // The ended op's sources are still loading; this one starts when they finish.
    if (running_) {
        queued_ = std::move(refresh);
        return handle;
    }
    running_ = std::move(refresh);
    run();
    return handle;
}

void CatalogService::run() {
    if (origin_) return revalidate();
    load_initial();
}

void CatalogService::load_initial() {
    const CancelToken token = running_->op->token();
    remote_.load(CatalogFetch::CacheOnly, token, [this, token, alive = alive_.token()](CatalogLoadResult cached) {
        if (alive.cancelled()) return;
        bundled_.load(CatalogFetch::CacheOnly, token, [this, alive, cached = std::move(cached)](CatalogLoadResult bundled) mutable {
            if (alive.cancelled()) return;
            if (!cached && !no_cache_yet(cached.error()))
                running_->warnings.push_back(as_warning(cached.error()));
            if (!bundled) running_->warnings.push_back(as_warning(bundled.error()));

            if (cached && bundled) {
                // On a tie the cache wins, since it may carry an ETag.
                if (bundled->catalog.serial > cached->catalog.serial) activate(std::move(*bundled));
                else activate(std::move(*cached));
            } else if (cached) {
                activate(std::move(*cached));
            } else if (bundled) {
                activate(std::move(*bundled));
            }
            revalidate();
        });
    });
}

void CatalogService::revalidate() {
    // A cancelled or timed-out op keeps what the local copies gave and skips the network.
    if (running_->op->token().cancelled()) return finish(std::nullopt);
    const bool expired = clock_.system_now() > current_.expires_at;
    if (origin_ && running_->mode == CatalogRefresh::IfExpired && !expired) return finish(std::nullopt);

    remote_.load(CatalogFetch::Revalidate, running_->op->token(), [this, alive = alive_.token()](CatalogLoadResult fetched) {
        if (alive.cancelled()) return;
        if (!fetched) {
            if (!origin_) return finish(to_diagnostic(fetched.error()));
            running_->warnings.push_back(as_warning(fetched.error()));
            return finish(std::nullopt);
        }
        activate(std::move(*fetched));
        finish(std::nullopt);
    });
}

void CatalogService::activate(LoadedCatalog loaded) {
    for (auto& warning : loaded.warnings) running_->warnings.push_back(std::move(warning));
    // One serial names one signed document, so an equal serial changes nothing.
    if (origin_ && loaded.catalog.serial <= current_.serial) {
        if (loaded.origin == CatalogOrigin::Remote && loaded.catalog.serial < current_.serial)
            running_->warnings.push_back(rollback_warning(loaded.catalog.serial, current_.serial));
        return;
    }

    current_ = std::move(loaded.catalog);
    origin_ = loaded.origin;
    aliases_ = AliasTable::for_catalog(current_);
    running_->changed = true;
}

CatalogUpdated CatalogService::describe(std::vector<Diagnostic> warnings) const {
    std::size_t installable = 0;
    for (const auto& entry : current_.entries)
        if (entry.installable()) ++installable;
    return CatalogUpdated{.serial = current_.serial,
                          .origin = origin_.value_or(CatalogOrigin::Bundled),
                          .entries = current_.entries.size(),
                          .installable = installable,
                          .warnings = std::move(warnings)};
}

void CatalogService::finish(std::optional<Diagnostic> failure) {
    Refresh refresh = std::move(*running_);
    running_.reset();

    if (origin_) {
        if (auto expiry = trust::check_expiry(trust::SignedDocumentKind::BuildCatalog, current_.expires_at,
                                              clock_.system_now()))
            refresh.warnings.push_back(std::move(*expiry));
    }
    if (refresh.changed) events_.publish(EventKind::CatalogChanged, describe(refresh.warnings));
    if (failure) {
        // Why neither local copy was usable.
        for (auto& warning : refresh.warnings) failure->causes.push_back(std::move(warning));
        refresh.op->complete(Failed{.error = std::move(*failure)});
    }
    else refresh.op->complete(Completed<CatalogUpdated>{.value = describe(std::move(refresh.warnings))});

    if (!queued_) return;
    running_ = std::move(queued_);
    queued_.reset();
    if (running_->op->done()) {
        release(*running_->op);
        running_.reset();
        return;
    }
    run();
}

}  // namespace rb::catalog
