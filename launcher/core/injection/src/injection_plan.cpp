#include "reboot/injection/injection_plan.hpp"

#include "reboot/injection/runtime_boot_default.hpp"

namespace rb::injection {

std::vector<ports::InjectEntry> InjectionPlan::inject_entries() const {
    std::vector<ports::InjectEntry> entries;
    entries.reserve(dlls.size());
    for (const PlannedDll& dll : dlls) entries.push_back(dll.entry);
    return entries;
}

std::optional<DllSlot> InjectionPlan::slot_of(const NativePath& path) const {
    for (const PlannedDll& dll : dlls)
        if (dll.entry.path == path) return dll.slot;
    return std::nullopt;
}

InjectionPlan plan_injection(const InjectionInputs& inputs) {
    const ports::BootStrategy strategy = resolve_boot_strategy(inputs.build_boot, inputs.runtime_boot);
    const auto planned = [&](DllSlot slot, const PinnedDll& dll) {
        return PlannedDll{slot, ports::InjectEntry{dll.path, dll.sha256, strategy, ports::InjectPhase::Early}};
    };

    InjectionPlan plan;
    plan.net_mode = inputs.custom_auth ? NetMode::LegacyFixed : NetMode::Isolated;
    plan.features = client_features(inputs.version, plan.net_mode);
    plan.dlls.push_back(planned(DllSlot::ClientRuntime, inputs.client_runtime));
    if (inputs.custom_auth) plan.dlls.push_back(planned(DllSlot::CustomAuth, *inputs.custom_auth));
    return plan;
}

}  // namespace rb::injection
