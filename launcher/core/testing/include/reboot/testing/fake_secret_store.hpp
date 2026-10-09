#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/secret_store.hpp"
#include "reboot/testing/fault_plan.hpp"

namespace rb::testing {

enum class SecretStoreOperation : u8 { Put, Get, Erase };

// Covers no capability ids (decision testing-strategy).
// ISecretStore in memory, wiping values on erase and destruction. Kind Unavailable fails every call
// with testing.secret_store_unavailable, as a Keychain outside Aqua or a missing libsecret does.
class FakeSecretStore final : public ports::ISecretStore {
public:
    explicit FakeSecretStore(ports::SecretStoreKind kind = ports::SecretStoreKind::Os) : kind_(kind) {}

    [[nodiscard]] ports::SecretStoreKind kind() const override;
    Result<void> put(std::string_view key, std::span<const u8> value) override;
    Result<std::optional<SecretBytes>> get(std::string_view key) override;
    Result<void> erase(std::string_view key) override;

    void set_kind(ports::SecretStoreKind kind);
    [[nodiscard]] bool contains(std::string_view key) const;
    // Keys only; values leave the store through get() alone.
    [[nodiscard]] std::vector<std::string> keys() const;

    [[nodiscard]] FaultPlan<SecretStoreOperation>& faults() noexcept { return faults_; }

private:
    mutable std::mutex mutex_;
    ports::SecretStoreKind kind_;
    std::map<std::string, SecretBytes, std::less<>> values_;
    FaultPlan<SecretStoreOperation> faults_;
};

}  // namespace rb::testing
