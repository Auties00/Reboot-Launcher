#include "reboot/integration/entry_error.hpp"

#include <string_view>
#include <utility>

#include "messages.hpp"

namespace reboot::integration {

namespace {

[[nodiscard]] std::string_view item_name(IntegrationKind kind) noexcept {
    switch (kind) {
        case IntegrationKind::UrlScheme: return "url_scheme";
        case IntegrationKind::Autostart: return "autostart";
        case IntegrationKind::EngineAgent: return "engine_agent";
        case IntegrationKind::DesktopEntry: return "desktop_entry";
    }
    return "unknown";
}

[[nodiscard]] Diagnostic finish(const EntryError& error, DiagBuilder&& builder, ErrorKind kind) {
    if (error.cause) std::move(builder).cause(*error.cause);
    return std::move(builder).kind(kind).build();
}

}  // namespace

Diagnostic to_diagnostic(const EntryError& error) {
    const auto diag = [&](MessageId message) {
        return make_diag(ErrorDomain::Integration, message).arg("item", item_name(error.kind));
    };
    switch (error.code) {
        case EntryErrorCode::NoItems:
            return finish(error, make_diag(ErrorDomain::Integration, msg::kNoItems), ErrorKind::InvalidInput);
        case EntryErrorCode::Foreign:
            return finish(error, diag(msg::kForeignEntry).arg("owner", error.owner), ErrorKind::Conflict);
        case EntryErrorCode::Unsupported: return finish(error, diag(msg::kUnsupported), ErrorKind::Unsupported);
        case EntryErrorCode::InspectFailed: return finish(error, diag(msg::kInspectFailed), ErrorKind::Generic);
        case EntryErrorCode::WriteFailed: return finish(error, diag(msg::kWriteFailed), ErrorKind::Generic);
        case EntryErrorCode::RemoveFailed: return finish(error, diag(msg::kRemoveFailed), ErrorKind::Generic);
        case EntryErrorCode::NotApplied: return finish(error, diag(msg::kNotApplied), ErrorKind::Generic);
    }
    return internal_bug("integration::to_diagnostic(EntryError)");
}

}  // namespace reboot::integration
