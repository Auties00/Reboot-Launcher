#include "reboot/testing/fake_secret_store.hpp"

#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace rb::testing {
namespace {

[[nodiscard]] Diagnostic unavailable() {
    return make_diag(kTestingDomain, msg::kSecretStoreUnavailable);
}

}  // namespace

ports::SecretStoreKind FakeSecretStore::kind() const {
    const std::scoped_lock lock(mutex_);
    return kind_;
}

Result<void> FakeSecretStore::put(std::string_view key, std::span<const u8> value) {
    if (auto error = faults_.take(SecretStoreOperation::Put)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    if (kind_ == ports::SecretStoreKind::Unavailable) return std::unexpected(unavailable());
    values_.insert_or_assign(std::string(key), SecretBytes(std::vector<u8>(value.begin(), value.end())));
    return {};
}

Result<std::optional<SecretBytes>> FakeSecretStore::get(std::string_view key) {
    if (auto error = faults_.take(SecretStoreOperation::Get)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    if (kind_ == ports::SecretStoreKind::Unavailable) return std::unexpected(unavailable());
    const auto it = values_.find(key);
    if (it == values_.end()) return std::optional<SecretBytes>();
    return std::optional<SecretBytes>(SecretBytes(it->second.reveal()));
}

Result<void> FakeSecretStore::erase(std::string_view key) {
    if (auto error = faults_.take(SecretStoreOperation::Erase)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    if (kind_ == ports::SecretStoreKind::Unavailable) return std::unexpected(unavailable());
    if (const auto it = values_.find(key); it != values_.end()) values_.erase(it);
    return {};
}

void FakeSecretStore::set_kind(ports::SecretStoreKind kind) {
    const std::scoped_lock lock(mutex_);
    kind_ = kind;
}

bool FakeSecretStore::contains(std::string_view key) const {
    const std::scoped_lock lock(mutex_);
    return values_.contains(key);
}

std::vector<std::string> FakeSecretStore::keys() const {
    const std::scoped_lock lock(mutex_);
    std::vector<std::string> out;
    out.reserve(values_.size());
    for (const auto& [key, value] : values_) out.push_back(key);
    return out;
}

}  // namespace rb::testing
