#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::ports {

enum class SecretStoreKind : u8 { Os, File, Unavailable };

// Blocking; called from the WorkerPool under a deadline.
class ISecretStore {
public:
    virtual ~ISecretStore() = default;

    [[nodiscard]] virtual SecretStoreKind kind() const = 0;
    virtual Result<void> put(std::string_view key, std::span<const u8> value) = 0;
    virtual Result<std::optional<SecretBytes>> get(std::string_view key) = 0;
    virtual Result<void> erase(std::string_view key) = 0;
};

}  // namespace reboot::ports
