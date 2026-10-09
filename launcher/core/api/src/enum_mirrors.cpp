// Schema enums that mirror C++ enums value for value; the engine casts between them.
#include <chrono>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "reboot/api/v1/common.hpp"
#include "reboot/api/v1/engine.hpp"
#include "reboot/api/v1/guidance.hpp"
#include "reboot/api/v1/integration.hpp"
#include "reboot/api/v1/logs.hpp"
#include "reboot/api/v1/requests.hpp"
#include "reboot/api/v1/settings.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/ports/os_services.hpp"
#include "reboot/ports/runner.hpp"

namespace rb::api {
namespace {

template <class Schema, class Cpp>
constexpr bool same(Schema schema, Cpp cpp) noexcept {
    return static_cast<u32>(std::to_underlying(schema)) == static_cast<u32>(std::to_underlying(cpp));
}

template <ArgKind kind>
using ArgAlternative = std::variant_alternative_t<std::to_underlying(kind), ::rb::Arg>;

}  // namespace

namespace wire = contracts::common;

static_assert(same(ArgKind::String, wire::ArgKind::String));
static_assert(same(ArgKind::Signed, wire::ArgKind::Signed));
static_assert(same(ArgKind::Unsigned, wire::ArgKind::Unsigned));
static_assert(same(ArgKind::Bool, wire::ArgKind::Bool));
static_assert(same(ArgKind::Millis, wire::ArgKind::Millis));
static_assert(same(ArgKind::Path, wire::ArgKind::Path));
static_assert(same(ArgKind::SemVer, wire::ArgKind::SemVer));
static_assert(std::is_same_v<ArgAlternative<ArgKind::String>, std::string>);
static_assert(std::is_same_v<ArgAlternative<ArgKind::Signed>, i64>);
static_assert(std::is_same_v<ArgAlternative<ArgKind::Unsigned>, u64>);
static_assert(std::is_same_v<ArgAlternative<ArgKind::Bool>, bool>);
static_assert(std::is_same_v<ArgAlternative<ArgKind::Millis>, std::chrono::milliseconds>);
static_assert(std::is_same_v<ArgAlternative<ArgKind::Path>, ::rb::WirePath>);
static_assert(std::is_same_v<ArgAlternative<ArgKind::SemVer>, ::rb::SemVer>);
static_assert(std::variant_size_v<::rb::Arg> == 7, "a new rb::Arg alternative needs an ArgKind value");

static_assert(same(OsErrorOrigin::Host, ::rb::SystemError::Origin::Host));
static_assert(same(OsErrorOrigin::GuestWindows, ::rb::SystemError::Origin::GuestWindows));

static_assert(same(ErrorKind::Generic, ::rb::ErrorKind::Generic));
static_assert(same(ErrorKind::InvalidInput, ::rb::ErrorKind::InvalidInput));
static_assert(same(ErrorKind::NotFound, ::rb::ErrorKind::NotFound));
static_assert(same(ErrorKind::Conflict, ::rb::ErrorKind::Conflict));
static_assert(same(ErrorKind::EngineUnavailable, ::rb::ErrorKind::EngineUnavailable));
static_assert(same(ErrorKind::Unsupported, ::rb::ErrorKind::Unsupported));
static_assert(same(ErrorKind::Cancelled, ::rb::ErrorKind::Cancelled));

static_assert(same(CancelReason::User, ::rb::CancelReason::User));
static_assert(same(CancelReason::Deadline, ::rb::CancelReason::Deadline));
static_assert(same(CancelReason::Shutdown, ::rb::CancelReason::Shutdown));
static_assert(same(CancelReason::Disconnect, ::rb::CancelReason::Disconnect));
static_assert(same(CancelReason::Superseded, ::rb::CancelReason::Superseded));

static_assert(same(LogLevel::Trace, ::rb::LogLevel::Trace));
static_assert(same(LogLevel::Debug, ::rb::LogLevel::Debug));
static_assert(same(LogLevel::Info, ::rb::LogLevel::Info));
static_assert(same(LogLevel::Warn, ::rb::LogLevel::Warn));
static_assert(same(LogLevel::Error, ::rb::LogLevel::Error));

static_assert(same(LogCategory::Engine, ::rb::LogCategory::Engine));
static_assert(same(LogCategory::Ipc, ::rb::LogCategory::Ipc));
static_assert(same(LogCategory::Net, ::rb::LogCategory::Net));
static_assert(same(LogCategory::Storage, ::rb::LogCategory::Storage));
static_assert(same(LogCategory::Builds, ::rb::LogCategory::Builds));
static_assert(same(LogCategory::Play, ::rb::LogCategory::Play));
static_assert(same(LogCategory::Host, ::rb::LogCategory::Host));
static_assert(same(LogCategory::Backend, ::rb::LogCategory::Backend));
static_assert(same(LogCategory::GameOutput, ::rb::LogCategory::GameOutput));
static_assert(same(LogCategory::Wine, ::rb::LogCategory::Wine));
static_assert(same(LogCategory::Browser, ::rb::LogCategory::Browser));
static_assert(same(LogCategory::Update, ::rb::LogCategory::Update));
static_assert(same(LogCategory::Client, ::rb::LogCategory::Client));
static_assert(same(LogCategory::Ui, ::rb::LogCategory::Ui));

static_assert(same(UserRequestKind::NeedsSecret, ::rb::UserRequestKind::NeedsSecret));
static_assert(same(UserRequestKind::ConfirmJoin, ::rb::UserRequestKind::ConfirmJoin));
static_assert(same(UserRequestKind::NeedsJoinPassword, ::rb::UserRequestKind::NeedsJoinPassword));
static_assert(same(UserRequestKind::AutoServerConsent, ::rb::UserRequestKind::AutoServerConsent));
static_assert(same(UserRequestKind::ConfirmUnencryptedUpstream, ::rb::UserRequestKind::ConfirmUnencryptedUpstream));
static_assert(same(UserRequestKind::AccountRenameConflict, ::rb::UserRequestKind::AccountRenameConflict));
static_assert(same(UserRequestKind::RosettaInstall, ::rb::UserRequestKind::RosettaInstall));
static_assert(same(UserRequestKind::AgentRequiresApproval, ::rb::UserRequestKind::AgentRequiresApproval));
static_assert(same(UserRequestKind::ChooseVersion, ::rb::UserRequestKind::ChooseVersion));
static_assert(same(UserRequestKind::ConfirmUntested, ::rb::UserRequestKind::ConfirmUntested));
static_assert(same(UserRequestKind::ConfirmStopSessions, ::rb::UserRequestKind::ConfirmStopSessions));

static_assert(same(RequestResolution::Answered, ::rb::RequestResolution::Answered));
static_assert(same(RequestResolution::Withdrawn, ::rb::RequestResolution::Withdrawn));

static_assert(same(RunnerKind::Native, ports::RunnerKind::Native));
static_assert(same(RunnerKind::Umu, ports::RunnerKind::Umu));
static_assert(same(RunnerKind::Wine, ports::RunnerKind::Wine));
static_assert(same(RunnerKind::MacRuntime, ports::RunnerKind::MacRuntime));

static_assert(same(IntegrationItem::UrlScheme, ports::IntegrationKind::UrlScheme));
static_assert(same(IntegrationItem::Autostart, ports::IntegrationKind::Autostart));
static_assert(same(IntegrationItem::EngineAgent, ports::IntegrationKind::EngineAgent));
static_assert(same(IntegrationItem::DesktopEntry, ports::IntegrationKind::DesktopEntry));

static_assert(same(Sensitivity::Public, ::rb::Sensitivity::Public));
static_assert(same(Sensitivity::Personal, ::rb::Sensitivity::Personal));
static_assert(same(Sensitivity::Secret, ::rb::Sensitivity::Secret));

static_assert(same(NoticeLevel::Info, ::rb::Severity::Info));
static_assert(same(NoticeLevel::Warning, ::rb::Severity::Warning));
static_assert(same(NoticeLevel::Error, ::rb::Severity::Error));

static_assert(same(StorageMode::ReadWrite, contracts::ipc::StorageMode::ReadWrite));
static_assert(same(StorageMode::ReadOnly, contracts::ipc::StorageMode::ReadOnly));
static_assert(same(StorageMode::InMemory, contracts::ipc::StorageMode::InMemory));

}  // namespace rb::api
