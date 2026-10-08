#include <catch2/catch_test_macros.hpp>
#include <optional>

#include "reboot/injection/client_features.hpp"
#include "reboot/injection/injection_plan.hpp"
#include "reboot/injection/runtime_boot_default.hpp"

using namespace reboot;
using namespace reboot::injection;

namespace {

using ports::BootStrategy;

constexpr RuntimeBootDefault kUnproven{};
constexpr RuntimeBootDefault kProvenAfterResume{BootStrategy::AfterResume};

InjectionInputs inputs(std::optional<PinnedDll> custom_auth = std::nullopt) {
    InjectionInputs result;
    result.version = GameVersion{12, 41, std::nullopt};
    result.runtime_boot = kNativeBootDefault;
    result.client_runtime = PinnedDll{"payload/rb_client.dll", {0x01}};
    result.custom_auth = std::move(custom_auth);
    return result;
}

const PinnedDll kCustom{"dlls/cobalt.dll", {0x02}};

}  // namespace

TEST_CASE("an unproven runtime forces AfterResume over the catalog", "[injection][boot]") {
    STATIC_CHECK(resolve_boot_strategy(std::nullopt, kUnproven) == BootStrategy::AfterResume);
    STATIC_CHECK(resolve_boot_strategy(BootStrategy::EarlyBirdApc, kUnproven) == BootStrategy::AfterResume);
}

TEST_CASE("a proven runtime defers to the catalog, else its own default", "[injection][boot]") {
    STATIC_CHECK(resolve_boot_strategy(std::nullopt, kNativeBootDefault) == BootStrategy::EarlyBirdApc);
    STATIC_CHECK(resolve_boot_strategy(BootStrategy::AfterResume, kNativeBootDefault) == BootStrategy::AfterResume);
    STATIC_CHECK(resolve_boot_strategy(BootStrategy::EarlyBirdApc, kProvenAfterResume) == BootStrategy::EarlyBirdApc);
}

TEST_CASE("client features follow the net mode and the major version", "[injection][features]") {
    constexpr ClientFeatures isolated = client_features(GameVersion{9, 10, std::nullopt}, NetMode::Isolated);
    STATIC_CHECK(isolated.auth_redirect);
    STATIC_CHECK(isolated.console);
    STATIC_CHECK(isolated.memory_fix);
    STATIC_CHECK(isolated.exit_suppression);

    constexpr ClientFeatures legacy = client_features(GameVersion{10, 0, std::nullopt}, NetMode::LegacyFixed);
    STATIC_CHECK(!legacy.auth_redirect);
    STATIC_CHECK(legacy.console);
    STATIC_CHECK(!legacy.memory_fix);
}

TEST_CASE("without a custom auth DLL only our DLL is injected, Isolated", "[injection][plan]") {
    const InjectionPlan plan = plan_injection(inputs());
    CHECK(plan.net_mode == NetMode::Isolated);
    CHECK(plan.features.auth_redirect);
    REQUIRE(plan.dlls.size() == 1);
    CHECK(plan.dlls[0].slot == DllSlot::ClientRuntime);
    CHECK(plan.dlls[0].entry.path == NativePath("payload/rb_client.dll"));
    CHECK(plan.dlls[0].entry.sha256[0] == 0x01);
    CHECK(plan.dlls[0].entry.strategy == BootStrategy::EarlyBirdApc);
    CHECK(plan.dlls[0].entry.phase == ports::InjectPhase::Early);
}

TEST_CASE("a custom auth DLL follows ours with the same strategy and switches to LegacyFixed", "[injection][plan]") {
    for (const RuntimeBootDefault runtime : {kNativeBootDefault, kUnproven}) {
        InjectionInputs in = inputs(kCustom);
        in.runtime_boot = runtime;
        const InjectionPlan plan = plan_injection(in);
        CHECK(plan.net_mode == NetMode::LegacyFixed);
        CHECK_FALSE(plan.features.auth_redirect);
        REQUIRE(plan.dlls.size() == 2);
        CHECK(plan.dlls[0].slot == DllSlot::ClientRuntime);
        CHECK(plan.dlls[1].slot == DllSlot::CustomAuth);
        CHECK(plan.dlls[1].entry.path == kCustom.path);
        CHECK(plan.dlls[1].entry.phase == ports::InjectPhase::Early);
        CHECK(plan.dlls[1].entry.strategy == plan.dlls[0].entry.strategy);
        CHECK(plan.dlls[1].entry.strategy == resolve_boot_strategy(std::nullopt, runtime));
    }
}

TEST_CASE("inject entries keep load order and slot_of names each path", "[injection][plan]") {
    const InjectionPlan plan = plan_injection(inputs(kCustom));
    const std::vector<ports::InjectEntry> entries = plan.inject_entries();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].path == NativePath("payload/rb_client.dll"));
    CHECK(entries[1].path == kCustom.path);

    CHECK(plan.slot_of("payload/rb_client.dll") == DllSlot::ClientRuntime);
    CHECK(plan.slot_of(kCustom.path) == DllSlot::CustomAuth);
    CHECK_FALSE(plan.slot_of("other.dll").has_value());
}
