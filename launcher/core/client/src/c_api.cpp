#include <chrono>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "abi_boundary.hpp"
#include "client_context.hpp"
#include "client_runtime.hpp"
#include "completion_latch.hpp"
#include "connect_settings.hpp"
#include "messages.hpp"
#include "reboot/client.h"
#include "wire/codec.hpp"

using reboot::Diagnostic;
using reboot::ErrorDomain;
using reboot::ErrorKind;
using reboot::Result;
using reboot::u8;
using reboot::u64;
using namespace reboot::client;

namespace {

[[nodiscard]] Diagnostic invalid_argument(std::string_view name) {
    return reboot::make_diag(ErrorDomain::Client, msg::kInvalidArgument).arg("name", name).kind(ErrorKind::InvalidInput);
}

// A null pointer is only valid with a zero size.
[[nodiscard]] std::optional<std::span<const u8>> input(const uint8_t* data, size_t size) {
    if (data == nullptr && size != 0) return std::nullopt;
    return std::span<const u8>{data, size};
}

[[nodiscard]] rb_status status_of(const Result<void>& result) {
    return result ? RB_OK : fail(result.error());
}

}  // namespace

extern "C" {

uint32_t rb_abi_version(void) noexcept { return uint32_t{RB_ABI_MAJOR} << 16 | uint32_t{RB_ABI_MINOR}; }

const char* rb_status_name(rb_status status) noexcept {
    switch (status) {
        case RB_OK: return "RB_OK";
        case RB_PENDING: return "RB_PENDING";
        case RB_E_TIMEOUT: return "RB_E_TIMEOUT";
        case RB_E_INVALID_ARG: return "RB_E_INVALID_ARG";
        case RB_E_REMOTE: return "RB_E_REMOTE";
        case RB_E_INTERNAL: return "RB_E_INTERNAL";
        case RB_E_LIMIT: return "RB_E_LIMIT";
        case RB_E_ENGINE_UNAVAILABLE: return "RB_E_ENGINE_UNAVAILABLE";
        case RB_E_ENGINE_ENDPOINT_UNTRUSTED: return "RB_E_ENGINE_ENDPOINT_UNTRUSTED";
        case RB_E_ENGINE_CANNOT_DETACH: return "RB_E_ENGINE_CANNOT_DETACH";
        case RB_E_ENGINE_VERSION_MISMATCH: return "RB_E_ENGINE_VERSION_MISMATCH";
        case RB_E_ENGINE_OTHER_SESSION: return "RB_E_ENGINE_OTHER_SESSION";
        case RB_E_ENGINE_ROOT_MISMATCH: return "RB_E_ENGINE_ROOT_MISMATCH";
        case RB_E_ELEVATED_AUTOSTART_REFUSED: return "RB_E_ELEVATED_AUTOSTART_REFUSED";
        case RB_E_NO_INTERACTIVE_SESSION: return "RB_E_NO_INTERACTIVE_SESSION";
        case RB_E_AGENT_REQUIRES_APPROVAL: return "RB_E_AGENT_REQUIRES_APPROVAL";
        case RB_E_CONNECTION_LOST: return "RB_E_CONNECTION_LOST";
        case RB_E_ABI_MISMATCH: return "RB_E_ABI_MISMATCH";
        case RB_E_CLOSED: return "RB_E_CLOSED";
        default: return "RB_UNKNOWN";
    }
}

rb_status rb_ctx_create(const rb_ctx_options* options, rb_ctx** out) noexcept {
    return guarded("rb_ctx_create", [&]() -> rb_status {
        if (out == nullptr) return fail(invalid_argument("out"));
        *out = nullptr;
        auto settings = read_connect_settings(options);
        if (!settings) return fail(settings.error());
        auto runtime = ClientRuntime::create();
        if (!runtime) return fail(runtime.error());
        auto context = ClientContext::create((*runtime)->deps(), std::move(*settings));
        if (!context) return fail(context.error());

        CompletionLatch<Result<Connected>> latch;
        (*context)->connect([&latch](Result<Connected> connected) { latch.set(std::move(connected)); });
        Result<Connected> connected = latch.wait();
        if (!connected) {
            (*context)->close();
            (*runtime)->stop();
            return fail(connected.error());
        }
        if (connected->image_warning) set_last_error(reboot::contracts::common::to_wire(*connected->image_warning));
        *out = new rb_ctx{std::move(*runtime), std::move(*context)};
        return RB_OK;
    });
}

void rb_ctx_destroy(rb_ctx* ctx) noexcept {
    guarded("rb_ctx_destroy", [&] {
        if (ctx == nullptr) return;
        ctx->context->close();
        ctx->runtime->stop();
        delete ctx;
    });
}

rb_status rb_call(rb_ctx* ctx, uint32_t method, const uint8_t* request, size_t request_size, uint32_t timeout_ms,
                  rb_buffer* response) noexcept {
    return guarded("rb_call", [&]() -> rb_status {
        const auto bytes = input(request, request_size);
        if (ctx == nullptr) return fail(invalid_argument("ctx"));
        if (!bytes) return fail(invalid_argument("request"));
        if (auto ok = check_output(response, "response"); !ok) return fail(ok.error());

        CompletionLatch<CallResult<std::vector<u8>>> latch;
        ctx->context->call(method, *bytes, std::chrono::milliseconds{timeout_ms},
                           [&latch](CallResult<std::vector<u8>> result) { latch.set(std::move(result)); });
        CallResult<std::vector<u8>> result = latch.wait();
        if (!result) {
            if (result.error().remote) fill_output(*response, sb::wire::encode_to_bytes(result.error().diagnostic));
            return fail(result.error());
        }
        fill_output(*response, std::move(*result));
        return RB_OK;
    });
}

rb_status rb_start(rb_ctx* ctx, uint32_t method, const uint8_t* request, size_t request_size, uint32_t flags,
                   uint64_t* op_id) noexcept {
    return guarded("rb_start", [&]() -> rb_status {
        const auto bytes = input(request, request_size);
        if (ctx == nullptr) return fail(invalid_argument("ctx"));
        if (!bytes) return fail(invalid_argument("request"));
        if (op_id == nullptr) return fail(invalid_argument("op_id"));
        if ((flags & ~(RB_START_DETACHED | RB_START_BOUND)) != 0 ||
            (flags & (RB_START_DETACHED | RB_START_BOUND)) == (RB_START_DETACHED | RB_START_BOUND))
            return fail(invalid_argument("flags"));
        std::optional<bool> detached;
        if (flags != 0) detached = (flags & RB_START_DETACHED) != 0;

        CompletionLatch<CallResult<u64>> latch;
        ctx->context->start(method, *bytes, detached,
                            [&latch](CallResult<u64> result) { latch.set(std::move(result)); });
        CallResult<u64> result = latch.wait();
        if (!result) {
            set_last_error(result.error().diagnostic);
            return start_status_for(result.error());
        }
        *op_id = *result;
        return RB_OK;
    });
}

rb_status rb_op_attach(rb_ctx* ctx, uint64_t op_id) noexcept {
    return guarded("rb_op_attach", [&]() -> rb_status {
        if (ctx == nullptr) return fail(invalid_argument("ctx"));
        return status_of(ctx->context->attach(op_id));
    });
}

rb_status rb_op_cancel(rb_ctx* ctx, uint64_t op_id) noexcept {
    return guarded("rb_op_cancel", [&]() -> rb_status {
        if (ctx == nullptr) return fail(invalid_argument("ctx"));
        return status_of(ctx->context->cancel(op_id));
    });
}

rb_status rb_op_result(rb_ctx* ctx, uint64_t op_id, rb_buffer* outcome) noexcept {
    return guarded("rb_op_result", [&]() -> rb_status {
        if (ctx == nullptr) return fail(invalid_argument("ctx"));
        if (auto ok = check_output(outcome, "outcome"); !ok) return fail(ok.error());
        auto state = ctx->context->op_state(op_id);
        if (!state) return fail(state.error());
        auto* bytes = std::get_if<std::vector<u8>>(&*state);
        if (bytes == nullptr) return RB_PENDING;
        fill_output(*outcome, std::move(*bytes));
        return RB_OK;
    });
}

rb_status rb_op_release(rb_ctx* ctx, uint64_t op_id) noexcept {
    return guarded("rb_op_release", [&]() -> rb_status {
        if (ctx == nullptr) return fail(invalid_argument("ctx"));
        return status_of(ctx->context->release(op_id));
    });
}

rb_status rb_subscribe(rb_ctx* ctx, const uint8_t* filter, size_t filter_size, uint64_t* sub) noexcept {
    return guarded("rb_subscribe", [&]() -> rb_status {
        const auto bytes = input(filter, filter_size);
        if (ctx == nullptr) return fail(invalid_argument("ctx"));
        if (!bytes) return fail(invalid_argument("filter"));
        if (sub == nullptr) return fail(invalid_argument("sub"));
        auto id = ctx->context->subscribe(*bytes);
        if (!id) return fail(id.error());
        *sub = *id;
        return RB_OK;
    });
}

rb_status rb_unsubscribe(rb_ctx* ctx, uint64_t sub) noexcept {
    return guarded("rb_unsubscribe", [&]() -> rb_status {
        if (ctx == nullptr) return fail(invalid_argument("ctx"));
        return status_of(ctx->context->unsubscribe(sub));
    });
}

rb_status rb_events_next(rb_ctx* ctx, uint64_t sub, uint32_t timeout_ms, rb_buffer* event) noexcept {
    return guarded("rb_events_next", [&]() -> rb_status {
        if (ctx == nullptr) return fail(invalid_argument("ctx"));
        if (auto ok = check_output(event, "event"); !ok) return fail(ok.error());
        std::optional<std::chrono::milliseconds> wait;
        if (timeout_ms != RB_WAIT_FOREVER) wait = std::chrono::milliseconds{timeout_ms};
        auto next = ctx->context->next_event(sub, wait);
        if (!next) return fail(next.error());
        if (!*next) return RB_PENDING;
        // contracts::ipc::WireEvent is layout-identical to reboot.api.v1 Event.
        fill_output(*event, sb::wire::encode_to_bytes(**next));
        return RB_OK;
    });
}

void rb_events_set_wake(rb_ctx* ctx, uint64_t sub, void (*wake)(uintptr_t user), uintptr_t user) noexcept {
    guarded("rb_events_set_wake", [&] {
        if (ctx != nullptr) ctx->context->set_wake(sub, WakeCallback{wake, user});
    });
}

rb_status rb_secret_put(rb_ctx* ctx, const uint8_t* target, size_t target_size, const uint8_t* secret,
                        size_t secret_size) noexcept {
    return guarded("rb_secret_put", [&]() -> rb_status {
        const auto target_bytes = input(target, target_size);
        const auto secret_bytes = input(secret, secret_size);
        if (ctx == nullptr) return fail(invalid_argument("ctx"));
        if (!target_bytes) return fail(invalid_argument("target"));
        if (!secret_bytes) return fail(invalid_argument("secret"));
        return status_of(ctx->context->put_secret(*target_bytes, *secret_bytes));
    });
}

rb_status rb_secret_reveal(rb_ctx* ctx, const uint8_t* target, size_t target_size, rb_buffer* out) noexcept {
    return guarded("rb_secret_reveal", [&]() -> rb_status {
        const auto bytes = input(target, target_size);
        if (ctx == nullptr) return fail(invalid_argument("ctx"));
        if (!bytes) return fail(invalid_argument("target"));
        if (auto ok = check_output(out, "out"); !ok) return fail(ok.error());
        CompletionLatch<CallResult<reboot::SecretBytes>> latch;
        ctx->context->reveal_secret(
            *bytes, [&latch](CallResult<reboot::SecretBytes> result) { latch.set(std::move(result)); });
        CallResult<reboot::SecretBytes> result = latch.wait();
        if (!result) return fail(result.error());
        const auto& secret = result->reveal();
        fill_output(*out, std::vector<u8>(secret.begin(), secret.end()));
        return RB_OK;
    });
}

void rb_log_write(rb_ctx* ctx, int32_t level, const char* utf8, size_t size) noexcept {
    guarded("rb_log_write", [&] {
        if (ctx == nullptr || (utf8 == nullptr && size != 0) || level < RB_LOG_TRACE || level > RB_LOG_ERROR) return;
        const std::string_view text = utf8 == nullptr ? std::string_view{} : std::string_view{utf8, size};
        ctx->context->log_write(static_cast<reboot::LogLevel>(level), text);
    });
}

rb_status rb_last_error(rb_buffer* diagnostic) noexcept {
    try {
        if (auto ok = check_output(diagnostic, "diagnostic"); !ok) return RB_E_INVALID_ARG;
        fill_output(*diagnostic, last_error());
        return RB_OK;
    } catch (...) {
        return RB_E_INTERNAL;
    }
}

void rb_buffer_release(rb_buffer* buffer) noexcept {
    if (buffer != nullptr) wipe_and_free(*buffer);
}

}  // extern "C"

static_assert(RB_LOG_TRACE == static_cast<int>(reboot::LogLevel::Trace));
static_assert(RB_LOG_ERROR == static_cast<int>(reboot::LogLevel::Error));
