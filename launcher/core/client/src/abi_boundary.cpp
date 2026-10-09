#include "abi_boundary.hpp"

#include <memory>
#include <utility>

#include "messages.hpp"
#include "reboot/foundation/secret.hpp"
#include "wire/codec.hpp"

namespace rb::client {

namespace {

thread_local std::vector<u8> t_last_error;

}  // namespace

void clear_last_error() noexcept {
    secure_wipe(t_last_error.data(), t_last_error.size());
    t_last_error.clear();
}

void set_last_error(const contracts::common::WireDiagnostic& diagnostic) {
    clear_last_error();
    t_last_error = sb::wire::encode_to_bytes(diagnostic);
}

const std::vector<u8>& last_error() noexcept { return t_last_error; }

rb_status fail(const CallFailure& failure) {
    set_last_error(failure.diagnostic);
    return status_for(failure);
}

rb_status fail(const Diagnostic& diagnostic) { return fail(local_failure(diagnostic)); }

Result<void> check_output(const rb_buffer* out, std::string_view name) {
    if (out == nullptr || out->data != nullptr || out->size != 0 || out->internal != nullptr)
        return make_diag(ErrorDomain::Client, msg::kInvalidArgument).arg("name", name).kind(ErrorKind::InvalidInput).fail();
    return {};
}

void fill_output(rb_buffer& out, std::vector<u8> bytes) {
    if (bytes.empty()) {
        out = rb_buffer{nullptr, 0, nullptr};
        return;
    }
    auto owned = std::make_unique<std::vector<u8>>(std::move(bytes));
    out = rb_buffer{owned->data(), owned->size(), owned.get()};
    static_cast<void>(owned.release());
}

void wipe_and_free(rb_buffer& buffer) noexcept {
    std::unique_ptr<std::vector<u8>> owned{static_cast<std::vector<u8>*>(buffer.internal)};
    if (owned) secure_wipe(owned->data(), owned->size());
    buffer = rb_buffer{nullptr, 0, nullptr};
}

}  // namespace rb::client
