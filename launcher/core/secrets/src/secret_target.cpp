#include "reboot/secrets/secret_target.hpp"

#include <charconv>
#include <expected>
#include <system_error>

#include "messages.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::secrets {

namespace {

std::unexpected<Diagnostic> invalid_scope(SecretKind kind, std::string_view scope) {
    return make_diag(ErrorDomain::Secrets, msg::kInvalidScope)
        .kind(ErrorKind::InvalidInput)
        .arg("scope", scope)
        .arg("kind", kind_name(kind))
        .fail();
}

}  // namespace

Result<SecretTarget> SecretTarget::parse(SecretKind kind, std::string_view scope) {
    switch (kind) {
        case SecretKind::RemoteBackendPassword: {
            const auto backend = parse_host_port(scope);
            if (!backend) return invalid_scope(kind, scope);
            return SecretTarget{kind, SecretScope::backend(*backend)};
        }
        case SecretKind::HostJoinPassword: {
            const auto profile = parse_uuid(scope);
            if (!profile) return invalid_scope(kind, scope);
            return SecretTarget{kind, SecretScope::host_profile(HostProfileId{*profile})};
        }
        case SecretKind::JoinPassword: {
            u64 request = 0;
            const char* end = scope.data() + scope.size();
            const auto [last, error] = std::from_chars(scope.data(), end, request);
            if (error != std::errc{} || last != end) return invalid_scope(kind, scope);
            return SecretTarget{kind, SecretScope::join_request(RequestId{request})};
        }
    }
    return invalid_scope(kind, scope);
}

}  // namespace rb::secrets
