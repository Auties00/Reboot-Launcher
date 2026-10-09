#include "reboot/gameserver/game_server_error.hpp"

#include <utility>

#include "messages.hpp"

namespace rb::gameserver {

namespace {

[[nodiscard]] Diagnostic finish(const GameServerError& error, DiagBuilder&& builder, ErrorKind kind) {
    if (error.cause) std::move(builder).cause(*error.cause);
    return std::move(builder).kind(kind).build();
}

[[nodiscard]] NativePath path_of(const GameServerError& error) { return error.path.value_or(NativePath{}); }

}  // namespace

Diagnostic to_diagnostic(const GameServerError& error) {
    const auto diag = [](MessageId message) { return make_diag(ErrorDomain::GameServer, message); };
    const auto with_path = [&](MessageId message) { return diag(message).arg("path", path_of(error)); };
    using enum GameServerErrorCode;
    switch (error.code) {
        case PathNotAbsolute: return finish(error, with_path(msg::kPathNotAbsolute), ErrorKind::InvalidInput);
        case ExeUnreadable: return finish(error, with_path(msg::kExeUnreadable), ErrorKind::NotFound);
        case DescribeSpawnFailed: return finish(error, with_path(msg::kDescribeSpawnFailed), ErrorKind::Generic);
        case DescribeTimeout:
            return finish(error,
                          with_path(msg::kDescribeTimeout)
                              .arg("timeout", error.timeout.value_or(std::chrono::milliseconds{0}))
                              .retryable(),
                          ErrorKind::Generic);
        case DescribeNoOutput: return finish(error, with_path(msg::kDescribeNoOutput), ErrorKind::Generic);
        case DescribeMalformed: return finish(error, with_path(msg::kDescribeMalformed), ErrorKind::Generic);
        case ProtocolMismatch:
            return finish(error,
                          with_path(msg::kProtocolMismatch)
                              .arg("actual", error.actual.value_or(0))
                              .arg("expected", error.expected.value_or(0)),
                          ErrorKind::Unsupported);
        case InvalidSockets:
            return finish(error, with_path(msg::kInvalidSockets).arg("actual", error.actual.value_or(0)),
                          ErrorKind::Unsupported);
        case DescriptionMismatch: return finish(error, with_path(msg::kDescriptionMismatch), ErrorKind::Generic);
        case PortCountMismatch:
            return finish(error,
                          diag(msg::kPortCountMismatch)
                              .arg("expected", error.expected.value_or(0))
                              .arg("actual", error.actual.value_or(0)),
                          ErrorKind::InvalidInput);
        case InvalidPort:
            return finish(error, diag(msg::kInvalidPort).arg("port", error.port.value_or(Port{}).value),
                          ErrorKind::InvalidInput);
        case BindNotIpv4:
            return finish(error, diag(msg::kBindNotIpv4).arg("address", error.address), ErrorKind::InvalidInput);
        case BackendRequired: return finish(error, diag(msg::kBackendRequired), ErrorKind::InvalidInput);
        case InvalidMatchSetting:
            return finish(error, diag(msg::kInvalidMatchSetting).arg("field", error.field), ErrorKind::InvalidInput);
        case InvalidAddress:
            return finish(error, diag(msg::kInvalidAddress).arg("address", error.address), ErrorKind::InvalidInput);
        case SessionDirFailed: return finish(error, with_path(msg::kSessionDirFailed), ErrorKind::Generic);
        case AlreadyStarted: return finish(error, diag(msg::kAlreadyStarted), ErrorKind::Conflict);
        case NotRunning: return finish(error, diag(msg::kNotRunning), ErrorKind::Conflict);
        case StoppedBeforeStart: return finish(error, diag(msg::kStoppedBeforeStart), ErrorKind::Cancelled);
        case CommandNotDeclared:
            return finish(error, diag(msg::kCommandNotDeclared).arg("command", error.command), ErrorKind::Unsupported);
        case CommandTimeout:
            return finish(error,
                          diag(msg::kCommandTimeout)
                              .arg("command", error.command)
                              .arg("timeout", error.timeout.value_or(std::chrono::milliseconds{0}))
                              .retryable(),
                          ErrorKind::Generic);
    }
    return internal_bug("gameserver.to_diagnostic");
}

}  // namespace rb::gameserver
