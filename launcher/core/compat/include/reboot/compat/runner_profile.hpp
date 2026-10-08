#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include "reboot/compat/runtime_id.hpp"
#include "reboot/components/component_ref.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/ports/runner.hpp"
#include "reboot/storage/enum_names.hpp"

namespace reboot::storage {

template <>
struct EnumNames<ports::RunnerKind> {
    static constexpr std::array<std::string_view, 4> kNames{"native", "umu", "wine", "mac_runtime"};
};

}  // namespace reboot::storage

namespace reboot::compat {

using RunnerKind = ports::RunnerKind;

// Covers no capability ids (decisions linux-compat-layer, macos-compat-layer, owner-2).
// The runner one play session runs under, resolved from the manifest by RuntimeService. Linux
// plays with Umu (GE-Proton) and falls back to Wine (Kron4ek); macOS plays with MacRuntime.
struct RunnerProfile {
    RunnerKind kind = RunnerKind::Native;
    // GE-Proton for Umu, Kron4ek for Wine, the CrossOver-source Wine + DXMT build for MacRuntime.
    RuntimeId runtime;
    // Umu only: the umu-launcher zipapp.
    std::optional<RuntimeId> launcher;
    // MacRuntime until this runtime completed a session: Rosetta translates it on first run.
    bool rosetta_cold = false;

    // Scales every OpKind deadline of the session: native x1, Wine x2, first run under Rosetta x4.
    [[nodiscard]] constexpr RunnerMultiplier multiplier() const noexcept {
        if (kind == RunnerKind::Native) return RunnerMultiplier::Native;
        return rosetta_cold ? RunnerMultiplier::RosettaFirstRun : RunnerMultiplier::Wine;
    }

    bool operator==(const RunnerProfile&) const = default;
};

// The runtime component that provides Wine for `kind`; nullopt for Native.
[[nodiscard]] constexpr std::optional<components::RuntimeKind> wine_runtime_kind(RunnerKind kind) noexcept {
    switch (kind) {
        case RunnerKind::Umu: return components::RuntimeKind::GeProton;
        case RunnerKind::Wine: return components::RuntimeKind::KronWine;
        case RunnerKind::MacRuntime: return components::RuntimeKind::MacWine;
        case RunnerKind::Native: break;
    }
    return std::nullopt;
}

// The directory name of the runner's prefix under data/prefixes, and its stored name.
[[nodiscard]] constexpr std::string_view runner_name(RunnerKind kind) noexcept {
    return storage::EnumNames<RunnerKind>::kNames[static_cast<std::size_t>(kind)];
}

}  // namespace reboot::compat
