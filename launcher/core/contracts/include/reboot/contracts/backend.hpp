#pragma once

#include <optional>
#include <string>
#include <vector>

#include "reboot/contracts/common.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

// reboot-backend --control=stdio. Requests carry req_id and get a typed reply or a
// common::CommandResult / common::Unsupported.
// Every loopback user can reach the listener, so the backend must:
// - bind exactly BackendWelcome::bind_address and exit non-zero when that fails;
// - keep mutable state only under BackendWelcome::data_root;
// - accept the X-Reboot-* headers only from the engine front;
// - never accept a fixed password such as "Rebooted";
// - redeem a launch credential once, before it expires, for its build, while its session is
//   configured and, on Linux, only from a peer with the backend's own uid;
// - serve game requests and XMPP only to tokens it issued;
// - answer GET /reboot/v1/backend-info.
namespace rb::contracts::backend {

inline constexpr u32 kBackendProtocol = VersionStreams::backend_protocol;

enum class AccountRole : u8 { Client, Host };
enum class CredentialKind : u8 { ExchangeCode, LaunchSecret };
// Report fails the rename and raises AccountRenameConflict.
enum class RenameConflictPolicy : u8 { Report, Overwrite, KeepTarget };

struct ContentVersion {
    u32 schema = 0;
    u64 serial = 0;
};

struct GameBuild {
    std::string version;
    u32 cl = 0;
};

// handshake

struct BackendHello {
    u32 protocol = 0;
    std::string build;
    ContentVersion content;
};
REBOOT_CONTRACT_FRAME(BackendHello, 0x200)

// `bind_address` is 127.0.0.1, or 0.0.0.0 when backend.allow_lan is set.
struct BackendWelcome {
    std::string bind_address;
    WirePath data_root;
    LogLevel log_level{};
};
REBOOT_CONTRACT_FRAME(BackendWelcome, 0x201)

// HTTP and WebSocket share this one OS-assigned listener.
struct Ready {
    u16 http_port = 0;
};
REBOOT_CONTRACT_FRAME(Ready, 0x202)

// engine -> backend

// Replayed after every backend restart.
struct RegisterAccount {
    u64 req_id = 0;
    std::string account_id;
    Uuid record_id;
    AccountRole role{};
};
REBOOT_CONTRACT_FRAME(RegisterAccount, 0x210)

struct RenameAccount {
    u64 req_id = 0;
    std::string old_account_id;
    std::string new_account_id;
    RenameConflictPolicy on_conflict{};
};
REBOOT_CONTRACT_FRAME(RenameAccount, 0x211)

// Single use, valid 5 minutes, bound to the build.
struct MintLaunchCredential {
    u64 req_id = 0;
    std::string account_id;
    CredentialKind kind{};
    u32 build_cl = 0;
};
REBOOT_CONTRACT_FRAME(MintLaunchCredential, 0x212)

// The engine moves `value` into a SecretString and registers it with the Redactor on receipt.
struct LaunchCredential {
    u64 req_id = 0;
    std::string value;
    u64 expires_at_unix_ms = 0;
};
REBOOT_CONTRACT_FRAME(LaunchCredential, 0x213)

// `origin` is the front's per-session origin, http://127.0.0.1:<port>/s/<session_key>/.
// Live sessions are replayed after a backend restart.
struct ConfigureSession {
    u64 req_id = 0;
    std::string session_key;
    std::string account_id;
    std::string origin;
    std::string console_key;
    GameBuild build;
};
REBOOT_CONTRACT_FRAME(ConfigureSession, 0x214)

// Revokes the session's unredeemed credentials and the tokens they were redeemed for.
struct EndSession {
    u64 req_id = 0;
    std::string session_key;
};
REBOOT_CONTRACT_FRAME(EndSession, 0x215)

struct AccountsList {
    u64 req_id = 0;
};
REBOOT_CONTRACT_FRAME(AccountsList, 0x216)

struct AccountsReset {
    u64 req_id = 0;
    std::string account_id;
};
REBOOT_CONTRACT_FRAME(AccountsReset, 0x217)

struct AccountsDelete {
    u64 req_id = 0;
    std::string account_id;
};
REBOOT_CONTRACT_FRAME(AccountsDelete, 0x218)

// No `role` prunes both roles.
struct AccountsPrune {
    u64 req_id = 0;
    u64 older_than_unix_ms = 0;
    std::optional<AccountRole> role;
};
REBOOT_CONTRACT_FRAME(AccountsPrune, 0x219)

struct AccountSummary {
    std::string account_id;
    std::optional<Uuid> record_id;
    AccountRole role{};
    std::string display_name;
    u64 last_login_unix_ms = 0;
};

struct AccountsReply {
    u64 req_id = 0;
    std::vector<AccountSummary> accounts;
};
REBOOT_CONTRACT_FRAME(AccountsReply, 0x21A)

struct PurgeData {
    u64 req_id = 0;
};
REBOOT_CONTRACT_FRAME(PurgeData, 0x21B)

struct ContentInfo {
    u64 req_id = 0;
};
REBOOT_CONTRACT_FRAME(ContentInfo, 0x21E)

struct ContentInfoReply {
    u64 req_id = 0;
    ContentVersion content;
};
REBOOT_CONTRACT_FRAME(ContentInfoReply, 0x21F)

struct Health {
    u64 req_id = 0;
};
REBOOT_CONTRACT_FRAME(Health, 0x220)

struct HealthReply {
    u64 req_id = 0;
    std::string version;
    ContentVersion content;
    u16 http_port = 0;
};
REBOOT_CONTRACT_FRAME(HealthReply, 0x221)

struct Drain {
    u64 req_id = 0;
};
REBOOT_CONTRACT_FRAME(Drain, 0x222)

// backend -> engine

// The engine answers with MatchTarget; pulled so nothing needs replaying after a restart.
struct ResolveMatchTarget {
    u64 req_id = 0;
    std::string account_id;
    std::string playlist;
};
REBOOT_CONTRACT_FRAME(ResolveMatchTarget, 0x230)

// No endpoint means no target is recorded for the account.
struct MatchTarget {
    u64 req_id = 0;
    std::optional<std::string> endpoint;
    std::optional<u16> beacon_port;
};
REBOOT_CONTRACT_FRAME(MatchTarget, 0x231)

// Fallback LoggedIn signal for sessions without our client DLL.
struct LoginObserved {
    std::string account_id;
    std::string session_key;
};
REBOOT_CONTRACT_FRAME(LoginObserved, 0x232)

struct AccountRenameConflict {
    std::string old_account_id;
    std::string new_account_id;
    std::optional<Uuid> existing_record_id;
};
REBOOT_CONTRACT_FRAME(AccountRenameConflict, 0x233)

}  // namespace rb::contracts::backend
