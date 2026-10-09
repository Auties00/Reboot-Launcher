#include "reboot/foundation/diag.hpp"

#include <algorithm>
#include <vector>

#include "messages.hpp"

namespace rb {

namespace {

// Constant-initialised, so registrations made during dynamic initialisation always find it.
constinit const detail::MessageRegistration* g_registrations = nullptr;

}  // namespace

detail::MessageRegistration::MessageRegistration(const MessageSpec& message) noexcept
    : spec(&message), next(g_registrations) {
    g_registrations = this;
}

std::span<const MessageSpec* const> message_registry() {
    static const std::vector<const MessageSpec*> registry = [] {
        std::vector<const MessageSpec*> specs;
        for (const detail::MessageRegistration* r = g_registrations; r != nullptr; r = r->next) specs.push_back(r->spec);
        std::ranges::sort(specs, {}, &MessageSpec::id);
        return specs;
    }();
    return registry;
}

int exit_code_for(const Diagnostic& diag) {
    switch (diag.kind) {
        case ErrorKind::Generic: return 1;
        case ErrorKind::InvalidInput: return 2;
        case ErrorKind::NotFound: return 3;
        case ErrorKind::Conflict: return 4;
        case ErrorKind::EngineUnavailable: return 5;
        case ErrorKind::Unsupported: return 6;
        case ErrorKind::Cancelled: return 130;
    }
    return 1;
}

Diagnostic internal_bug(std::string_view where) {
    return make_diag(ErrorDomain::Internal, msg::kInternalBug).arg("where", where).build();
}

}  // namespace rb
