#include "entry_ops.hpp"

#include <optional>
#include <string>
#include <utility>

#include "reboot/integration/entry_error.hpp"
#include "reboot/integration/integration_policy.hpp"
#include "reboot/ports/os_services.hpp"

namespace rb::integration {

namespace {

[[nodiscard]] Diagnostic entry_error(EntryErrorCode code, IntegrationKind kind,
                                     std::optional<Diagnostic> cause = std::nullopt, std::string owner = {}) {
    return to_diagnostic(EntryError{.code = code, .kind = kind, .owner = std::move(owner), .cause = std::move(cause)});
}

[[nodiscard]] EntryStatus with_detail(EntryStatus status, Diagnostic detail) {
    status.detail = std::move(detail);
    return status;
}

[[nodiscard]] bool unsupported(const Diagnostic& error) noexcept { return error.kind == ErrorKind::Unsupported; }

}  // namespace

EntryStatus inspect_entry(ports::IIntegrationRegistrar& registrar, const IntegrationTargets& targets,
                          IntegrationKind kind) {
    EntryStatus status{.kind = kind};
    if (!entry_program(kind, targets)) {
        status.state = EntryState::Unsupported;
        return status;
    }
    Result<ports::IntegrationStatus> found = registrar.status(kind);
    if (!found) {
        const bool not_here = unsupported(found.error());
        status.state = not_here ? EntryState::Unsupported : EntryState::Unknown;
        status.detail = entry_error(not_here ? EntryErrorCode::Unsupported : EntryErrorCode::InspectFailed, kind,
                                    std::move(found.error()));
        return status;
    }
    status.state = classify(*found, targets.flavor);
    // An inactive entry's detail is the registrar's marker, not what it runs.
    if (status.state == EntryState::Ours || status.state == EntryState::Stale || status.state == EntryState::Foreign)
        status.target = std::move(found->detail);
    return status;
}

EntryStatus write_entry(ports::IIntegrationRegistrar& registrar, const IntegrationTargets& targets,
                        const EntryStatus& found) {
    const std::optional<NativePath> program = entry_program(found.kind, targets);
    if (!program) {
        EntryStatus status{.kind = found.kind, .state = EntryState::Unsupported};
        return with_detail(std::move(status), entry_error(EntryErrorCode::Unsupported, found.kind));
    }
    if (Result<void> written = registrar.apply(found.kind, *program); !written) {
        if (unsupported(written.error())) {
            EntryStatus status{.kind = found.kind, .state = EntryState::Unsupported};
            return with_detail(std::move(status),
                               entry_error(EntryErrorCode::Unsupported, found.kind, std::move(written.error())));
        }
        return with_detail(found, entry_error(EntryErrorCode::WriteFailed, found.kind, std::move(written.error())));
    }
    EntryStatus after = inspect_entry(registrar, targets, found.kind);
    if (!after.detail && !is_registered(after.state))
        after.detail = entry_error(EntryErrorCode::NotApplied, found.kind);
    return after;
}

EntryStatus apply_entry(ports::IIntegrationRegistrar& registrar, const IntegrationTargets& targets,
                        const EntryStatus& found) {
    switch (found.state) {
        case EntryState::Absent:
        case EntryState::Stale: return write_entry(registrar, targets, found);
        case EntryState::Foreign:
            return with_detail(found, entry_error(EntryErrorCode::Foreign, found.kind, std::nullopt, found.target));
        case EntryState::Unsupported:
            if (found.detail) return found;
            return with_detail(found, entry_error(EntryErrorCode::Unsupported, found.kind));
        case EntryState::Ours:
        case EntryState::Disabled:
        case EntryState::AwaitingApproval:
        case EntryState::Unknown: return found;
    }
    return found;
}

EntryStatus remove_entry(ports::IIntegrationRegistrar& registrar, const IntegrationTargets& targets,
                         const EntryStatus& found) {
    switch (found.state) {
        case EntryState::Absent:
        case EntryState::Unknown: return found;
        case EntryState::Foreign:
            return with_detail(found, entry_error(EntryErrorCode::Foreign, found.kind, std::nullopt, found.target));
        case EntryState::Unsupported:
            if (found.detail) return found;
            return with_detail(found, entry_error(EntryErrorCode::Unsupported, found.kind));
        case EntryState::Ours:
        case EntryState::Disabled:
        case EntryState::AwaitingApproval:
        case EntryState::Stale: break;
    }
    if (Result<void> removed = registrar.remove(found.kind); !removed) {
        // The entry stays as found, e.g. a scheme macOS declares in the bundle.
        const EntryErrorCode code =
            unsupported(removed.error()) ? EntryErrorCode::Unsupported : EntryErrorCode::RemoveFailed;
        return with_detail(found, entry_error(code, found.kind, std::move(removed.error())));
    }
    EntryStatus after = inspect_entry(registrar, targets, found.kind);
    if (!after.detail && after.state != EntryState::Absent && after.state != EntryState::Foreign)
        after.detail = entry_error(EntryErrorCode::RemoveFailed, found.kind);
    return after;
}

}  // namespace rb::integration
