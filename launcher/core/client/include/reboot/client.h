#pragma once

/* reboot_client: the public, versioned C ABI to the per-user Reboot Launcher engine.
 * Covers no capability ids; every capability is a reboot.api.v1 method reached through rb_call
 * and rb_start by its RB_METHOD_* id (client_methods.h), with payloads this library passes through
 * undecoded. Engine details (build, pid, image path, data root) come from RB_METHOD_ENGINE_STATUS
 * and RB_METHOD_ENGINE_INFO, whose schema_fingerprint a facade compares with RB_SCHEMA_FINGERPRINT.
 *
 * Any function may be called from any thread, concurrently, except rb_ctx_destroy: it must be
 * the last call on its context, with no other thread still inside a function given that context.
 * Inputs are borrowed for the duration of the call. Outputs are rb_buffers owned by the library
 * and freed only by rb_buffer_release. An output buffer must be zeroed or released on entry; one
 * that still holds data is RB_E_INVALID_ARG. */

#include <stddef.h>
#include <stdint.h>

#include "reboot/client_methods.h"

#define RB_ABI_MAJOR 1
#define RB_ABI_MINOR 0

#if defined(_WIN32)
#if defined(REBOOT_CLIENT_BUILD)
#define RB_API __declspec(dllexport)
#else
#define RB_API __declspec(dllimport)
#endif
#else
#define RB_API __attribute__((visibility("default")))
#endif

#if defined(__cplusplus)
#define RB_NOEXCEPT noexcept
extern "C" {
#else
#define RB_NOEXCEPT
#endif

/* An int32_t rather than an enum, so a facade can receive a code added by a later minor. */
typedef int32_t rb_status;

/* rb_last_error names the Diagnostic id behind every code below RB_OK. */
enum {
    RB_OK = 0,
    /* The op has no outcome yet, or no event arrived before the timeout. */
    RB_PENDING = 1,

    /* client.call_timed_out. */
    RB_E_TIMEOUT = -1,
    /* client.invalid_argument, ipc.unknown_op or ipc.unknown_subscription. */
    RB_E_INVALID_ARG = -2,
    /* The engine refused the request with its own Diagnostic. */
    RB_E_REMOTE = -3,
    /* internal.bug: the library itself failed, for example out of memory. */
    RB_E_INTERNAL = -4,
    /* ipc.too_many_calls or ipc.too_many_subscriptions. */
    RB_E_LIMIT = -5,

    /* ipc.engine_unavailable or ipc.update_in_progress. */
    RB_E_ENGINE_UNAVAILABLE = -100,
    /* ipc.endpoint_untrusted. */
    RB_E_ENGINE_ENDPOINT_UNTRUSTED = -101,
    /* ipc.engine_cannot_detach. Windows: run `reboot-engine run --foreground`. */
    RB_E_ENGINE_CANNOT_DETACH = -102,
    /* ipc.version_mismatch: the engine is another build and serves only the bootstrap subset. */
    RB_E_ENGINE_VERSION_MISMATCH = -103,
    /* rb_start only: the engine runs in another OS session than the caller, so it cannot play for it. */
    RB_E_ENGINE_OTHER_SESSION = -104,
    /* ipc.root_mismatch: the endpoint's engine serves another data root. */
    RB_E_ENGINE_ROOT_MISMATCH = -105,

    /* ipc.elevated_autostart_refused. */
    RB_E_ELEVATED_AUTOSTART_REFUSED = -200,
    /* ipc.no_interactive_session. */
    RB_E_NO_INTERACTIVE_SESSION = -201,
    /* ipc.agent_requires_approval. macOS: the login item needs approval; open the app once. */
    RB_E_AGENT_REQUIRES_APPROVAL = -202,

    /* ipc.connection_lost or ipc.engine_closed. */
    RB_E_CONNECTION_LOST = -300,
    /* client.abi_mismatch. */
    RB_E_ABI_MISMATCH = -301,
    /* client.closed: the context or subscription is closed and will not reconnect. */
    RB_E_CLOSED = -302
};

typedef struct rb_ctx rb_ctx;

typedef struct rb_buffer {
    const uint8_t* data;
    size_t size;
    /* Owned by reboot_client; NULL when the buffer is empty. */
    void* internal;
} rb_buffer;

/* Values mirror the engine's client kinds. */
enum {
    RB_CLIENT_UNKNOWN = 0,
    RB_CLIENT_WINDOWS_GUI = 1,
    RB_CLIENT_MAC_GUI = 2,
    RB_CLIENT_LINUX_GUI = 3,
    RB_CLIENT_CLI = 4,
    RB_CLIENT_TEST = 5
};

enum {
    /* Starts the engine when none answers, and reconnects after a loss. */
    RB_LAUNCH_AUTOSTART = 0,
    /* Never starts an engine and never reconnects. */
    RB_LAUNCH_CONNECT_ONLY = 1
};

/* Fields are only appended; a newer caller's extra fields must be zero for an older library. */
typedef struct rb_ctx_options {
    uint32_t struct_size;
    /* UTF-8, NUL-terminated and absolute; NULL means REBOOT_LAUNCHER_HOME, then the platform default. */
    const char* data_root;
    uint32_t client_kind;
    uint32_t launch_mode;
    /* 0 means 10000. */
    uint32_t connect_deadline_ms;
} rb_ctx_options;

/* rb_start flags; with neither, the method's default disconnect policy applies. */
#define RB_START_DETACHED 0x1u
#define RB_START_BOUND 0x2u

/* rb_events_next timeout that never expires. */
#define RB_WAIT_FOREVER UINT32_MAX

enum {
    RB_LOG_TRACE = 0,
    RB_LOG_DEBUG = 1,
    RB_LOG_INFO = 2,
    RB_LOG_WARN = 3,
    RB_LOG_ERROR = 4
};

/* (RB_ABI_MAJOR << 16) | RB_ABI_MINOR of the loaded library; facades refuse a major mismatch. */
RB_API uint32_t rb_abi_version(void) RB_NOEXCEPT;

/* A static, never freed name such as "RB_E_TIMEOUT"; "RB_UNKNOWN" for an unknown code. */
RB_API const char* rb_status_name(rb_status status) RB_NOEXCEPT;

/* Resolves the data root, captures the caller's OS session once, then connects to that root's
 * engine, starting it first in RB_LAUNCH_AUTOSTART mode. On failure *out stays NULL. On RB_OK
 * rb_last_error may still hold a warning (ipc.engine_image_differs). */
RB_API rb_status rb_ctx_create(const rb_ctx_options* options, rb_ctx** out) RB_NOEXCEPT;

/* Closes the connection; ops bound to it are cancelled by the engine. Waits for a running wake
 * callback, so it must not be called from one. After it returns no wake runs. NULL is ignored. */
RB_API void rb_ctx_destroy(rb_ctx* ctx) RB_NOEXCEPT;

/* A reboot.api.v1 CALL method. `request` and `response` are that method's encoded messages.
 * timeout_ms 0 sends no deadline: the engine applies the method's default and the call waits for
 * the reply or a lost connection. RB_E_REMOTE puts the Diagnostic in `response`. */
RB_API rb_status rb_call(rb_ctx* ctx, uint32_t method, const uint8_t* request, size_t request_size,
                         uint32_t timeout_ms, rb_buffer* response) RB_NOEXCEPT;

/* A reboot.api.v1 OPERATION method. On RB_OK the op is attached to this context. Bad input is
 * refused synchronously (RB_E_REMOTE or RB_E_ENGINE_OTHER_SESSION, Diagnostic through
 * rb_last_error) and creates no op. */
RB_API rb_status rb_start(rb_ctx* ctx, uint32_t method, const uint8_t* request, size_t request_size,
                          uint32_t flags, uint64_t* op_id) RB_NOEXCEPT;

/* Attaches an op started elsewhere, such as by another UI or before a reconnect.
 * RB_METHOD_ENGINE_OPERATIONS lists the live ones, and the OpStarted event announces each new one. */
RB_API rb_status rb_op_attach(rb_ctx* ctx, uint64_t op_id) RB_NOEXCEPT;

/* Idempotent for an attached op; the outcome still arrives through rb_op_result. */
RB_API rb_status rb_op_cancel(rb_ctx* ctx, uint64_t op_id) RB_NOEXCEPT;

/* RB_PENDING until terminal, then always the same encoded reboot.api.v1 Outcome. */
RB_API rb_status rb_op_result(rb_ctx* ctx, uint64_t op_id, rb_buffer* outcome) RB_NOEXCEPT;

/* Detaches the op; the engine keeps its outcome until every handle is released. */
RB_API rb_status rb_op_release(rb_ctx* ctx, uint64_t op_id) RB_NOEXCEPT;

/* `filter` is an encoded reboot.api.v1 EventFilter; empty matches every event. */
RB_API rb_status rb_subscribe(rb_ctx* ctx, const uint8_t* filter, size_t filter_size, uint64_t* sub) RB_NOEXCEPT;

/* A caller blocked in rb_events_next on `sub` returns RB_E_CLOSED. Waits for a running wake
 * callback of `sub`, so it must not be called from that callback. */
RB_API rb_status rb_unsubscribe(rb_ctx* ctx, uint64_t sub) RB_NOEXCEPT;

/* Pops one encoded reboot.api.v1 Event, waiting up to timeout_ms (0 polls). RB_PENDING when none
 * arrived; RB_E_CLOSED once the subscription is closed and empty.
 * For an op attached to this context, a subscription whose filter admits that op's OpCompleted
 * and names no session receives that OpCompleted exactly once, across Resync and reconnects. */
RB_API rb_status rb_events_next(rb_ctx* ctx, uint64_t sub, uint32_t timeout_ms, rb_buffer* event) RB_NOEXCEPT;

/* `wake` carries no event. It runs on a library thread with no lock held that other functions
 * take, at most once until the queue has been popped empty, and must not block. If events are
 * already queued it runs soon after this call. NULL removes it. */
RB_API void rb_events_set_wake(rb_ctx* ctx, uint64_t sub, void (*wake)(uintptr_t user), uintptr_t user) RB_NOEXCEPT;

/* Hands a secret to the engine in its own frame; `target` is an encoded reboot.api.v1
 * SecretTarget. The library wipes its copy once written. */
RB_API rb_status rb_secret_put(rb_ctx* ctx, const uint8_t* target, size_t target_size, const uint8_t* secret,
                               size_t secret_size) RB_NOEXCEPT;

/* Reads back a host join password, the only revealable secret. rb_buffer_release wipes it. */
RB_API rb_status rb_secret_reveal(rb_ctx* ctx, const uint8_t* target, size_t target_size, rb_buffer* out) RB_NOEXCEPT;

/* Writes a UTF-8 line to the engine log under the client category; secrets are redacted there. */
RB_API void rb_log_write(rb_ctx* ctx, int32_t level, const char* utf8, size_t size) RB_NOEXCEPT;

/* The encoded reboot.api.v1 Diagnostic of this thread's last call: its failure, or the warning
 * of a successful rb_ctx_create; otherwise an empty buffer. */
RB_API rb_status rb_last_error(rb_buffer* diagnostic) RB_NOEXCEPT;

/* Wipes, then frees, and leaves the buffer zeroed. NULL and empty buffers are ignored. */
RB_API void rb_buffer_release(rb_buffer* buffer) RB_NOEXCEPT;

#if defined(__cplusplus)
}
#endif
