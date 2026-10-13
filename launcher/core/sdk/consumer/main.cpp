// Uses the SDK as a client does, with no engine running: the library loads, the ABI matches, the
// method ids agree with the C header, payloads round-trip, and a connect-only context fails with a
// typed, English error.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#include "reboot/sdk/sdk.hpp"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++failures;
}

}  // namespace

int main() {
    using namespace std::chrono_literals;
    check((rb_abi_version() >> 16) == RB_ABI_MAJOR, "reboot_client has this ABI major");
    check(rb::api::kEngineInfo == RB_METHOD_ENGINE_INFO, "method ids agree with client_methods.h");

    rb::api::SessionsStopRequest request;
    request.grace_ms = 250;
    const auto decoded = rb::api::decode<rb::api::SessionsStopRequest>(rb::api::encode(request));
    check(decoded && decoded->grace_ms == 250, "a request round-trips through the codec");

    rb::sdk::ConnectOptions options;
    options.data_root = (std::filesystem::temp_directory_path() / "reboot-sdk-consumer-no-engine").string();
    options.client_kind = rb::sdk::ClientKind::Test;
    options.launch_mode = rb::sdk::LaunchMode::ConnectOnly;
    options.connect_deadline = 500ms;
    const rb::sdk::Expected<rb::sdk::Context> context = rb::sdk::Context::connect(options);
    check(!context, "connect-only finds no engine");
    if (!context) {
        check(context.error().status() == RB_E_ENGINE_UNAVAILABLE, "the failure is RB_E_ENGINE_UNAVAILABLE");
        const std::string english = context.error().english();
        check(!english.empty() && english != context.error().id(), "the failure reads as English");
        std::printf("%s: %s\n", context.error().id().c_str(), english.c_str());
    }

    rb::sdk::QueueDispatcher dispatcher;
    bool ran = false;
    dispatcher.post([&ran] { ran = true; });
    check(dispatcher.run_pending() == 1 && ran, "the dispatcher runs what is posted");

    if (failures != 0) {
        std::printf("sdk consumer: %d failed\n", failures);
        return 1;
    }
    std::printf("sdk consumer: ok\n");
    return 0;
}
