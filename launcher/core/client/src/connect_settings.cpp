#include "connect_settings.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>

#include "messages.hpp"
#include "reboot/foundation/text.hpp"

namespace rb::client {

namespace {

[[nodiscard]] Diagnostic invalid(std::string_view name) {
    return make_diag(ErrorDomain::Client, msg::kInvalidArgument).arg("name", name).kind(ErrorKind::InvalidInput);
}

[[nodiscard]] Result<std::string> read_utf8(const char* text, std::string_view name) {
    const std::string_view view{text};
    if (!is_valid_utf8(view)) return std::unexpected(invalid(name));
    return std::string{view};
}

}  // namespace

Result<ConnectSettings> read_connect_settings(const rb_ctx_options* options) {
    if (options == nullptr || options->struct_size < kOptionsSizeV1_0) return std::unexpected(invalid("options"));
    // A newer caller's fields are only safe to ignore while they hold zero.
    const std::span<const u8> tail{reinterpret_cast<const u8*>(options) + kOptionsSizeV1_0,
                                   options->struct_size - kOptionsSizeV1_0};
    if (std::ranges::any_of(tail, [](u8 byte) { return byte != 0; }))
        return make_diag(ErrorDomain::Client, msg::kAbiMismatch)
            .arg("abi_version", u32{RB_ABI_MAJOR} << 16 | u32{RB_ABI_MINOR})
            .kind(ErrorKind::Unsupported)
            .fail();

    ConnectSettings settings;
    if (options->data_root != nullptr) {
        auto root = read_utf8(options->data_root, "data_root");
        if (!root) return std::unexpected(std::move(root.error()));
        NativePath path{std::u8string{root->begin(), root->end()}};
        // A relative root would resolve against the client's directory here and the engine's there.
        if (!path.is_absolute()) return std::unexpected(invalid("data_root"));
        settings.data_root = std::move(path);
    }
    if (options->client_kind > RB_CLIENT_TEST) return std::unexpected(invalid("client_kind"));
    settings.client_kind = static_cast<contracts::ipc::ClientKind>(options->client_kind);
    if (options->launch_mode > RB_LAUNCH_CONNECT_ONLY) return std::unexpected(invalid("launch_mode"));
    settings.launch_mode = static_cast<ipc::LaunchMode>(options->launch_mode);
    if (options->connect_deadline_ms != 0)
        settings.connect_deadline = std::chrono::milliseconds{options->connect_deadline_ms};
    return settings;
}

}  // namespace rb::client
