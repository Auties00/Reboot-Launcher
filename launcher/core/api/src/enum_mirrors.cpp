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

namespace reboot::api {
namespace {

template <class Schema, class Cpp>
constexpr bool same(Schema schema, Cpp cpp) noexcept {
    return static_cast<u32>(std::to_underlying(schema)) == static_cast<u32>(std::to_underlying(cpp));
}

template <ArgKind kind>
using ArgAlternative = std::variant_alternative_t<std::to_underlying(kind), ::reboot::Arg>;

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
static_assert(std::is_same_v<ArgAlternative<ArgKind::Path>, ::reboot::WirePath>);
static_assert(std::is_same_v<ArgAlternative<ArgKind::SemVer>, ::reboot::SemVer>);
static_assert(std::variant_size_v<::reboot::Arg> == 7, "a new reboot::Arg alternative needs an ArgKind value");

static_assert(same(OsErrorOrigin::Host, ::reboot::SystemError::Origin::Host));
static_assert(same(OsErrorOrigin::GuestWindows, ::reboot::SystemError::Origin::GuestWindows));

static_assert(same(ErrorKind::Generic, ::reboot::ErrorKind::Generic));
static_assert(same(ErrorKind::InvalidInput, ::reboot::ErrorKind::InvalidInput));
static_assert(same(ErrorKind::NotFound, ::reboot::ErrorKind::NotFound));
static_assert(same(ErrorKind::Conflict, ::reboot::ErrorKind::Conflict));
static_assert(same(ErrorKind::EngineUnavailable, ::reboot::ErrorKind::EngineUnavailable));
static_assert(same(ErrorKind::Unsupported, ::reboot::ErrorKind::Unsupported));
static_assert(same(ErrorKind::Cancelled, ::reboot::ErrorKind::Cancelled));

static_assert(same(CancelReason::User, ::reboot::CancelReason::User));
static_assert(same(CancelReason::Deadline, ::reboot::CancelReason::Deadline));
static_assert(same(CancelReason::Shutdown, ::reboot::CancelReason::Shutdown));
static_assert(same(CancelReason::Disconnect, ::reboot::CancelReason::Disconnect));
static_assert(same(CancelReason::Superseded, ::reboot::CancelReason::Superseded));

static_assert(same(LogLevel::Trace, ::reboot::LogLevel::Trace));
static_assert(same(LogLevel::Debug, ::reboot::LogLevel::Debug));
static_assert(same(LogLevel::Info, ::reboot::LogLevel::Info));
static_assert(same(LogLevel::Warn, ::reboot::LogLevel::Warn));
static_assert(same(LogLevel::Error, ::reboot::LogLevel::Error));

static_assert(same(LogCategory::Engine, ::reboot::LogCategory::Engine));
static_assert(same(LogCategory::Ipc, ::reboot::LogCategory::Ipc));
static_assert(same(LogCategory::Net, ::reboot::LogCategory::Net));
static_assert(same(LogCategory::Storage, ::reboot::LogCategory::Storage));
static_assert(same(LogCategory::Builds, ::reboot::LogCategory::Builds));
static_assert(same(LogCategory::Play, ::reboot::LogCategory::Play));
static_assert(same(LogCategory::Host, ::reboot::LogCategory::Host));
static_assert(same(LogCategory::Backend, ::reboot::LogCategory::Backend));
static_assert(same(LogCategory::GameOutput, ::reboot::LogCategory::GameOutput));
static_assert(same(LogCategory::Wine, ::reboot::LogCategory::Wine));
static_assert(same(LogCategory::Browser, ::reboot::LogCategory::Browser));
static_assert(same(LogCategory::Update, ::reboot::LogCategory::Update));
static_assert(same(LogCategory::Client, ::reboot::LogCategory::Client));
static_assert(same(LogCategory::Ui, ::reboot::LogCategory::Ui));

static_assert(same(UserRequestKind::NeedsSecret, ::reboot::UserRequestKind::NeedsSecret));
static_assert(same(UserRequestKind::ConfirmJoin, ::reboot::UserRequestKind::ConfirmJoin));
static_assert(same(UserRequestKind::NeedsJoinPassword, ::reboot::UserRequestKind::NeedsJoinPassword));
static_assert(same(UserRequestKind::AutoServerConsent, ::reboot::UserRequestKind::AutoServerConsent));
static_assert(same(UserRequestKind::ConfirmUnencryptedUpstream, ::reboot::UserRequestKind::ConfirmUnencryptedUpstream));
static_assert(same(UserRequestKind::AccountRenameConflict, ::reboot::UserRequestKind::AccountRenameConflict));
static_assert(same(UserRequestKind::RosettaInstall, ::reboot::UserRequestKind::RosettaInstall));
static_assert(same(UserRequestKind::AgentRequiresApproval, ::reboot::UserRequestKind::AgentRequiresApproval));
static_assert(same(UserRequestKind::ChooseVersion, ::reboot::UserRequestKind::ChooseVersion));
static_assert(same(UserRequestKind::ConfirmUntested, ::reboot::UserRequestKind::ConfirmUntested));
static_assert(same(UserRequestKind::ConfirmStopSessions, ::reboot::UserRequestKind::ConfirmStopSessions));

static_assert(same(RequestResolution::Answered, ::reboot::RequestResolution::Answered));
static_assert(same(RequestResolution::Withdrawn, ::reboot::RequestResolution::Withdrawn));

static_assert(same(RunnerKind::Native, ports::RunnerKind::Native));
static_assert(same(RunnerKind::Umu, ports::RunnerKind::Umu));
static_assert(same(RunnerKind::Wine, ports::RunnerKind::Wine));
static_assert(same(RunnerKind::MacRuntime, ports::RunnerKind::MacRuntime));

static_assert(same(IntegrationItem::UrlScheme, ports::IntegrationKind::UrlScheme));
static_assert(same(IntegrationItem::Autostart, ports::IntegrationKind::Autostart));
static_assert(same(IntegrationItem::EngineAgent, ports::IntegrationKind::EngineAgent));
static_assert(same(IntegrationItem::DesktopEntry, ports::IntegrationKind::DesktopEntry));

static_assert(same(Sensitivity::Public, ::reboot::Sensitivity::Public));
static_assert(same(Sensitivity::Personal, ::reboot::Sensitivity::Personal));
static_assert(same(Sensitivity::Secret, ::reboot::Sensitivity::Secret));

static_assert(same(NoticeLevel::Info, ::reboot::Severity::Info));
static_assert(same(NoticeLevel::Warning, ::reboot::Severity::Warning));
static_assert(same(NoticeLevel::Error, ::reboot::Severity::Error));

static_assert(same(StorageMode::ReadWrite, contracts::ipc::StorageMode::ReadWrite));
static_assert(same(StorageMode::ReadOnly, contracts::ipc::StorageMode::ReadOnly));
static_assert(same(StorageMode::InMemory, contracts::ipc::StorageMode::InMemory));

}  // namespace reboot::api
