#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "reboot/base/diag.hpp"
#include "reboot/base/secret.hpp"
#include "reboot/base/types.hpp"

namespace rb::ports {

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

}  // namespace rb::ports
