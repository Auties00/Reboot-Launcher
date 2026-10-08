#pragma once

#include <utility>

#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/platform_services.hpp"
#include "reboot/testing/fake_caller_context.hpp"
#include "reboot/testing/fake_engine_starter.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/in_memory_ipc.hpp"

namespace reboot::testing {

// Covers no capability ids (decisions testing-strategy, async-event-model).
// The ClientPlatform make_client_platform() would return, built from fakes; its connector reaches
// the engine through `ipc`, which the engine's FakePlatform may share.
class FakeClientPlatform {
public:
    explicit FakeClientPlatform(InMemoryIpc& ipc, NativePath base = default_fake_root());
    FakeClientPlatform(const FakeClientPlatform&) = delete;
    FakeClientPlatform& operator=(const FakeClientPlatform&) = delete;
    FakeClientPlatform(FakeClientPlatform&&) = delete;
    FakeClientPlatform& operator=(FakeClientPlatform&&) = delete;

    // ClientContext owns its platform: the typed views below then live only as long as that
    // ClientContext. Call once.
    [[nodiscard]] ports::ClientPlatform take() noexcept { return std::move(platform_); }

    [[nodiscard]] FakeEngineStarter& starter() noexcept { return *starter_; }
    [[nodiscard]] FakeCallerContext& caller() noexcept { return *caller_; }
    [[nodiscard]] FakePlatformPaths& paths() noexcept { return *paths_; }

private:
    ports::ClientPlatform platform_;
    FakeEngineStarter* starter_ = nullptr;
    FakeCallerContext* caller_ = nullptr;
    FakePlatformPaths* paths_ = nullptr;
};

}  // namespace reboot::testing
