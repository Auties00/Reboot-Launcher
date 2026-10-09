#include "game_channel_error.hpp"

#include <string_view>
#include <utility>

#include "messages.hpp"
#include "peer_role.hpp"

namespace rb::game_channel {

namespace {

[[nodiscard]] std::string_view role_of(const std::optional<contracts::game_client::PeerRole>& role) {
    return role ? role_name(*role) : std::string_view("game component");
}

}  // namespace

Diagnostic to_diagnostic(const GameChannelError& error) {
    using Code = GameChannelErrorCode;
    const auto build = [&](MessageId message) { return make_diag(ErrorDomain::GameChannel, message); };
    Diagnostic diag;
    switch (error.code) {
        case Code::ListenFailed: diag = build(msg::kListenFailed).retryable(); break;
        case Code::NotListening: diag = build(msg::kNotListening).kind(ErrorKind::Conflict); break;
        case Code::BadPreamble: diag = build(msg::kBadPreamble).kind(ErrorKind::InvalidInput); break;
        case Code::PayloadAbiMismatch:
            diag = build(msg::kPayloadAbiMismatch)
                       .arg("actual", error.actual_version.value_or(0))
                       .arg("expected", error.expected_version.value_or(0))
                       .kind(ErrorKind::Unsupported);
            break;
        case Code::ProtocolMismatch:
            diag = build(msg::kProtocolMismatch)
                       .arg("role", role_of(error.role))
                       .arg("actual", error.actual_version.value_or(0))
                       .arg("expected", error.expected_version.value_or(0))
                       .kind(ErrorKind::Unsupported);
            break;
        case Code::HelloTimeout:
            diag = build(msg::kHelloTimeout).arg("timeout", error.timeout.value_or(std::chrono::milliseconds{0}));
            break;
        case Code::UnknownToken: diag = build(msg::kUnknownToken).kind(ErrorKind::InvalidInput); break;
        case Code::RoleMismatch:
            diag = build(msg::kRoleMismatch)
                       .arg("actual", role_of(error.presented_role))
                       .arg("expected", role_of(error.role))
                       .kind(ErrorKind::InvalidInput);
            break;
        case Code::DuplicatePeer:
            diag = build(msg::kDuplicatePeer).arg("role", role_of(error.role)).arg("module", error.module).kind(ErrorKind::Conflict);
            break;
        case Code::PeerLost: diag = build(msg::kPeerLost).arg("role", role_of(error.role)); break;
        case Code::NotWelcomed: diag = build(msg::kNotWelcomed).arg("role", role_of(error.role)).kind(ErrorKind::Conflict); break;
        case Code::UnsupportedRequest:
            diag = build(msg::kUnsupportedRequest)
                       .arg("role", role_of(error.role))
                       .arg("request", error.request)
                       .kind(ErrorKind::Unsupported);
            break;
        case Code::RequestFailed:
            diag = build(msg::kRequestFailed).arg("role", role_of(error.role)).arg("request", error.request);
            break;
        case Code::TestModeOff: diag = build(msg::kTestModeOff).arg("request", error.request).kind(ErrorKind::Unsupported); break;
        case Code::LogUnreadable: diag = build(msg::kLogUnreadable).arg("path", error.path.value_or(NativePath{})); break;
    }
    if (error.os_error) diag.os_error = error.os_error;
    if (error.cause) diag.causes.push_back(*error.cause);
    return diag;
}

}  // namespace rb::game_channel
