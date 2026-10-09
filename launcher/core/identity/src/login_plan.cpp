#include "reboot/identity/login_plan.hpp"

#include <cstddef>

#include "messages.hpp"
#include "reboot/identity/third_party_login.hpp"
#include "reboot/storage/enum_names.hpp"

namespace reboot::identity {

namespace {

[[nodiscard]] std::unexpected<Diagnostic> invalid(MessageId message) {
    return make_diag(ErrorDomain::Identity, message).kind(ErrorKind::InvalidInput).fail();
}

[[nodiscard]] std::string suffixed_account_id(const AccountRecord& record) {
    return account_id(record) + std::string(kLoginDomainSuffix);
}

}  // namespace

std::string effective_login(const AccountRecord& record, const LoginTarget& target) {
    if (record.role == AccountRole::Host) return account_id(record);
    if (target.backend == storage::BackendKind::Embedded) return suffixed_account_id(record);
    if (target.remote_login) return *target.remote_login;
    if (target.flavor == UpstreamFlavor::Reboot) return suffixed_account_id(record);
    return third_party_login(record.display_name);
}

Result<LoginPlan> plan_login(const AccountRecord& record, const LoginTarget& target, bool build_takes_exchangecode) {
    if (record.role != AccountRole::Client) return std::unexpected(internal_bug("identity.plan_login: host record"));

    LoginPlan plan;
    plan.auth_login = effective_login(record, target);
    plan.auth_type = AuthType::Epic;

    if (target.backend == storage::BackendKind::Embedded) {
        if (target.policy == CredentialPolicy::LegacyArgv) return invalid(msg::kLegacyArgvNeedsHostedBackend);
        if (build_takes_exchangecode) plan.auth_type = AuthType::ExchangeCode;
        plan.delivery = BackendMinted{build_takes_exchangecode ? contracts::backend::CredentialKind::ExchangeCode
                                                               : contracts::backend::CredentialKind::LaunchSecret};
        return plan;
    }

    if (target.remote_login && target.remote_login->empty())
        return make_diag(ErrorDomain::Identity, msg::kEmptyRemoteLogin)
            .kind(ErrorKind::InvalidInput)
            .arg("backend", storage::EnumNames<storage::BackendKind>::kNames[static_cast<std::size_t>(target.backend)])
            .fail();
    const bool has_login = target.remote_login.has_value();

    if (target.policy == CredentialPolicy::LegacyArgv) {
        if (!target.custom_auth_dll) return invalid(msg::kLegacyArgvNeedsCustomAuthDll);
        if (!has_login) return invalid(msg::kLegacyArgvNeedsLogin);
        plan.delivery = LegacyArgv{};
        plan.warnings.push_back(make_diag(ErrorDomain::Identity, msg::kPasswordInArgv).severity(Severity::Warning));
        return plan;
    }

    if (target.flavor == UpstreamFlavor::Reboot && has_login && build_takes_exchangecode) {
        plan.auth_type = AuthType::ExchangeCode;
        plan.delivery = RemotePasswordExchange{};
        return plan;
    }
    plan.delivery =
        FrontTicket{has_login ? TicketRedemption::SwapForStoredPassword : TicketRedemption::PassThrough};
    return plan;
}

}  // namespace reboot::identity
