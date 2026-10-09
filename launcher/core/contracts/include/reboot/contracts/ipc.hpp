#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "reboot/contracts/common.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

// Engine IPC. API payloads (Call, Reply, Start, OpResult, events) are reboot.api.v1 bytes that
// the client library never decodes. Ping and Pong are the common messages.
namespace rb::contracts::ipc {

using Bytes = std::vector<u8>;

enum class ClientKind : u8 { Unknown, WindowsGui, MacGui, LinuxGui, Cli, Test };
enum class EngineOrigin : u8 { OnDemand, ServiceManager, Foreground };
enum class StorageMode : u8 { ReadWrite, ReadOnly, InMemory };
enum class Compatibility : u8 { Full, BootstrapOnly };
enum class GoodbyeReason : u8 { Normal, Shutdown, Restarting, SlowConsumer, ProtocolError, VersionMismatch };

struct EnvVar {
    std::string name;
    std::string value;
};

struct CallerContext {
    std::string os_session;
    bool elevated = false;
    std::vector<EnvVar> display_env;
};

// client -> engine

struct Hello {
    ClientKind client_kind{};
    std::string client_build;
    u32 abi_version = 0;
    u32 pid = 0;
    CallerContext caller_context;
};
REBOOT_CONTRACT_FRAME(Hello, 0x100)

struct Call {
    u64 req_id = 0;
    u32 method_id = 0;
    Bytes payload;
    u32 timeout_ms = 0;
};
REBOOT_CONTRACT_FRAME(Call, 0x101)

// No `detached` value means the method's default disconnect policy.
struct Start {
    u64 req_id = 0;
    u32 method_id = 0;
    Bytes payload;
    std::optional<bool> detached;
};
REBOOT_CONTRACT_FRAME(Start, 0x102)

struct Cancel {
    u64 op_id = 0;
};
REBOOT_CONTRACT_FRAME(Cancel, 0x103)

struct Attach {
    u64 op_id = 0;
};
REBOOT_CONTRACT_FRAME(Attach, 0x104)

struct Release {
    u64 op_id = 0;
};
REBOOT_CONTRACT_FRAME(Release, 0x105)

// `filter` is an encoded reboot.api.v1 EventFilter.
struct Subscribe {
    u64 sub_id = 0;
    Bytes filter;
};
REBOOT_CONTRACT_FRAME(Subscribe, 0x106)

struct Unsubscribe {
    u64 sub_id = 0;
};
REBOOT_CONTRACT_FRAME(Unsubscribe, 0x107)

struct Credit {
    u64 sub_id = 0;
    u32 n = 0;
};
REBOOT_CONTRACT_FRAME(Credit, 0x108)

// The engine wipes the frame buffer once the secret is stored.
struct SecretPut {
    Bytes target;
    Bytes bytes;
};
REBOOT_CONTRACT_FRAME(SecretPut, 0x109)

struct SecretReveal {
    u64 req_id = 0;
    Bytes target;
};
REBOOT_CONTRACT_FRAME(SecretReveal, 0x10A)

struct LogWrite {
    LogLevel level{};
    std::string text;
};
REBOOT_CONTRACT_FRAME(LogWrite, 0x10B)

// Either direction.
struct Goodbye {
    GoodbyeReason reason{};
};
REBOOT_CONTRACT_FRAME(Goodbye, 0x10C)

// engine -> client

struct HelloAck {
    std::string engine_build;
    u64 epoch = 0;
    u32 pid = 0;
    WirePath image_path;
    WirePath canonical_root;
    EngineOrigin origin{};
    StorageMode storage_mode{};
    bool secrets_available = false;
    Compatibility compatibility{};
};
REBOOT_CONTRACT_FRAME(HelloAck, 0x120)

// Choice: payload | error.
struct Reply {
    u64 req_id = 0;
    std::optional<Bytes> payload;
    std::optional<common::WireDiagnostic> error;
};
REBOOT_CONTRACT_FRAME(Reply, 0x121)

struct Started {
    u64 req_id = 0;
    u64 op_id = 0;
};
REBOOT_CONTRACT_FRAME(Started, 0x122)

// `outcome` is an encoded reboot.api.v1 Outcome.
struct OpResult {
    u64 op_id = 0;
    Bytes outcome;
};
REBOOT_CONTRACT_FRAME(OpResult, 0x123)

// `kind` is the reboot.api.v1 event kind.
struct WireEvent {
    u32 kind = 0;
    u64 epoch = 0;
    u64 seq = 0;
    std::optional<Uuid> session;
    std::optional<u64> op;
    Bytes payload;
};

struct EventBatch {
    u64 sub_id = 0;
    std::vector<WireEvent> events;
};
REBOOT_CONTRACT_FRAME(EventBatch, 0x124)

struct Resync {
    u64 sub_id = 0;
};
REBOOT_CONTRACT_FRAME(Resync, 0x125)

struct ForegroundHint {
    u32 pid = 0;
};
REBOOT_CONTRACT_FRAME(ForegroundHint, 0x126)

// method_id = service << 16 | method; the reboot.api.v1 method options must use these values.
[[nodiscard]] constexpr u32 method_id(u16 service, u16 method) noexcept { return u32{service} << 16 | method; }

inline constexpr u32 kEngineStatus = method_id(1, 1);
inline constexpr u32 kEngineDrain = method_id(1, 3);
inline constexpr u32 kEngineShutdown = method_id(1, 4);
inline constexpr u32 kEngineRestartWhenIdle = method_id(1, 5);
inline constexpr u32 kSessionsList = method_id(12, 1);
inline constexpr u32 kSessionsStop = method_id(12, 4);

// Frozen: these methods and Hello/HelloAck never change layout, so any engine build serves them.
inline constexpr std::array<u32, 6> kBootstrapMethodIds{kEngineStatus,          kEngineDrain,  kEngineShutdown,
                                                        kEngineRestartWhenIdle, kSessionsList, kSessionsStop};

inline constexpr std::size_t kMaxSubscriptions = 64;
inline constexpr std::size_t kMaxOutstandingCalls = 256;
inline constexpr std::size_t kOutboundBudget = std::size_t{32} << 20;
// Hitting the outbound budget twice within this window disconnects with SlowConsumer.
inline constexpr std::chrono::seconds kSlowConsumerWindow{10};

}  // namespace rb::contracts::ipc
