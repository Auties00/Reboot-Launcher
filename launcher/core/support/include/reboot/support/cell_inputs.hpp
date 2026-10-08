#pragma once

#include <string>
#include <variant>

#include "reboot/components/component_ref.hpp"
#include "reboot/contracts/backend.hpp"

namespace reboot::support {

// `runner_pin` is the pinned runtime component id; empty for the Native runner.
struct PlayCellInputs {
    components::Sha256Digest client_dll_sha256{};
    contracts::backend::ContentVersion backend_content;
    std::string runner_pin;

    // Out of line: the contract's ContentVersion has no operator== to default from.
    [[nodiscard]] bool operator==(const PlayCellInputs& other) const noexcept;
};

// Hosting runs natively, so the game-server binary is the only input.
struct HostCellInputs {
    components::Sha256Digest game_server_sha256{};

    bool operator==(const HostCellInputs&) const = default;
};

// The exact inputs a Tested verdict holds for; any change drops the cell to Untested.
using CellInputs = std::variant<PlayCellInputs, HostCellInputs>;

}  // namespace reboot::support
