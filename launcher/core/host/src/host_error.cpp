#include "reboot/host/host_error.hpp"

#include <string>
#include <utility>

#include "messages.hpp"
#include "reboot/host/host_profile.hpp"
#include "reboot/host/match_end_policy.hpp"
#include "reboot/publish/field_limits.hpp"

namespace reboot::host {

namespace {

[[nodiscard]] std::string profile_text(const HostError& error) {
    return error.profile ? format_uuid(error.profile->value) : std::string();
}

[[nodiscard]] std::string session_text(const HostError& error) {
    return error.session ? format_uuid(error.session->value) : std::string();
}

[[nodiscard]] u16 port_value(const HostError& error) { return error.port ? error.port->value : u16{0}; }

[[nodiscard]] Diagnostic finish(const HostError& error, DiagBuilder&& builder, ErrorKind kind) {
    if (error.os_error) std::move(builder).os(*error.os_error);
    if (error.detail) std::move(builder).detail(*error.detail);
    if (error.cause) std::move(builder).cause(*error.cause);
    return std::move(builder).kind(kind).build();
}

}  // namespace

Diagnostic to_diagnostic(const HostError& error) {
    const auto diag = [](MessageId message) { return make_diag(ErrorDomain::Host, message); };
    using enum HostErrorCode;
    switch (error.code) {
        case ProfileNotFound:
            return finish(error, diag(msg::kProfileNotFound).arg("profile", profile_text(error)), ErrorKind::NotFound);
        case ProfileNameEmpty: return finish(error, diag(msg::kProfileNameEmpty), ErrorKind::InvalidInput);
        case ProfileNameTooLong:
            return finish(error, diag(msg::kProfileNameTooLong).arg("limit", kMaxProfileNameLength),
                          ErrorKind::InvalidInput);
        case ProfileNameTaken:
            return finish(error, diag(msg::kProfileNameTaken).arg("name", error.name), ErrorKind::Conflict);
        case ServerNameTooLong:
            return finish(error, diag(msg::kServerNameTooLong).arg("limit", publish::kMaxServerNameBytes),
                          ErrorKind::InvalidInput);
        case DescriptionTooLong:
            return finish(error, diag(msg::kDescriptionTooLong).arg("limit", publish::kMaxDescriptionBytes),
                          ErrorKind::InvalidInput);
        case ProfileStale: return finish(error, diag(msg::kProfileStale).arg("name", error.name), ErrorKind::Conflict);
        case BuiltinProfile:
            return finish(error, diag(msg::kBuiltinProfile).arg("name", error.name), ErrorKind::InvalidInput);
        case AutoProfileListed: return finish(error, diag(msg::kAutoProfileListed), ErrorKind::InvalidInput);
        case InvalidPortPolicy:
            return finish(error, diag(msg::kInvalidPortPolicy).arg("min", kMinHostPort.value),
                          ErrorKind::InvalidInput);
        case ReservedPort:
            return finish(error, diag(msg::kReservedPort).arg("port", port_value(error)), ErrorKind::InvalidInput);
        case InvalidMatchEndDelay:
            return finish(error, diag(msg::kInvalidMatchEndDelay).arg("limit", kMaxMatchEndDelay),
                          ErrorKind::InvalidInput);
        case InvalidOperatorAddress:
            return finish(error, diag(msg::kInvalidOperatorAddress).arg("address", error.address),
                          ErrorKind::InvalidInput);
        case BanWithoutTarget: return finish(error, diag(msg::kBanWithoutTarget), ErrorKind::InvalidInput);
        case ProfileBusy: return finish(error, diag(msg::kProfileBusy).arg("name", error.name), ErrorKind::Conflict);
        case HostLimitReached:
            return finish(error, diag(msg::kHostLimitReached).arg("limit", error.limit.value_or(0)),
                          ErrorKind::Conflict);
        case LinkedNeedsAutoProfile:
            return finish(error, diag(msg::kLinkedNeedsAutoProfile), ErrorKind::InvalidInput);
        case AutoProfileNeedsLink: return finish(error, diag(msg::kAutoProfileNeedsLink), ErrorKind::InvalidInput);
        case NoBuildSelected: return finish(error, diag(msg::kNoBuildSelected), ErrorKind::InvalidInput);
        case BuildVersionUnknown:
            return finish(error, diag(msg::kBuildVersionUnknown).arg("name", error.name), ErrorKind::Unsupported);
        case BlockOutOfRange:
            return finish(error,
                          diag(msg::kBlockOutOfRange)
                              .arg("block_size", error.block_size.value_or(0))
                              .arg("port", port_value(error)),
                          ErrorKind::InvalidInput);
        case NoFreeBlock: {
            const PortRange range = error.range.value_or(PortRange{});
            return finish(error,
                          diag(msg::kNoFreeBlock)
                              .arg("block_size", error.block_size.value_or(0))
                              .arg("first", range.first.value)
                              .arg("last", range.last.value)
                              .retryable(),
                          ErrorKind::Conflict);
        }
        case ListenTimeout: return finish(error, diag(msg::kListenTimeout).retryable(), ErrorKind::Generic);
        case PortNotOwned:
            return finish(error, diag(msg::kPortNotOwned).arg("port", port_value(error)), ErrorKind::Conflict);
        case ReadinessTimeout:
            return finish(error, diag(msg::kReadinessTimeout).arg("port", port_value(error)).retryable(),
                          ErrorKind::Generic);
        case NotHostSession:
            return finish(error, diag(msg::kNotHostSession).arg("session", session_text(error)),
                          ErrorKind::NotFound);
        case ServerNotRunning: return finish(error, diag(msg::kServerNotRunning), ErrorKind::Conflict);
        case NotListening:
            return finish(error, diag(msg::kNotListening).arg("session", session_text(error)).retryable(),
                          ErrorKind::Conflict);
        case BuildAndVersion: return finish(error, diag(msg::kBuildAndVersion), ErrorKind::InvalidInput);
        case BlockInUse:
            return finish(error, diag(msg::kBlockInUse).arg("port", port_value(error)).retryable(), ErrorKind::Conflict);
        case Cancelled: return finish(error, diag(msg::kCancelled), ErrorKind::Cancelled);
        case UntestedDeclined: return finish(error, diag(msg::kUntestedDeclined), ErrorKind::Cancelled);
        case InvalidAnswer: return finish(error, diag(msg::kInvalidAnswer), ErrorKind::InvalidInput);
        case ListenFailed:
            return finish(error, diag(msg::kListenFailed).arg("port", port_value(error)), ErrorKind::Conflict);
        case ServerExited:
            return finish(error, diag(msg::kServerExited).arg("code", error.exit_code.value_or(0)), ErrorKind::Generic);
        case ServerFatal: return finish(error, diag(msg::kServerFatal).arg("code", error.name), ErrorKind::Generic);
        case ServerUnresponsive: return finish(error, diag(msg::kServerUnresponsive), ErrorKind::Generic);
    }
    return internal_bug("host_error.to_diagnostic");
}

}  // namespace reboot::host
