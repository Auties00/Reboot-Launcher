#include "reboot/game_channel/token_registry.hpp"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#include "game_channel_error.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/foundation/secret.hpp"

namespace reboot::game_channel {

namespace {

constexpr std::chrono::milliseconds kHelloBase{2000};

[[nodiscard]] std::span<const u8> text_bytes(const SecretString& text) noexcept {
    const std::string& value = text.reveal();
    return {reinterpret_cast<const u8*>(value.data()), value.size()};
}

}  // namespace

struct TokenRegistry::Impl {
    struct Entry {
        PeerKey key;
        Secret<std::array<u8, kControlTokenSize>> bytes;
        SecretString env_value;
        RunnerMultiplier multiplier = RunnerMultiplier::Native;
        bool claimed = false;
    };

    Impl(IRandom& random_ref, Redactor& redactor_ref) : random(random_ref), redactor(redactor_ref) {}

    IRandom& random;
    Redactor& redactor;
    std::vector<Entry> entries;
};

TokenRegistry::TokenRegistry(IRandom& random, Redactor& redactor) : impl_(std::make_unique<Impl>(random, redactor)) {}

TokenRegistry::~TokenRegistry() {
    for (const Impl::Entry& entry : impl_->entries) impl_->redactor.remove_secret(text_bytes(entry.env_value));
}

Result<ControlToken> TokenRegistry::issue(PeerKey key, RunnerMultiplier multiplier) {
    const bool taken = std::ranges::any_of(impl_->entries, [&](const Impl::Entry& entry) { return entry.key == key; });
    if (taken)
        return std::unexpected(to_diagnostic(
            GameChannelError{.code = GameChannelErrorCode::DuplicatePeer, .role = key.role, .module = key.module}));

    std::array<u8, kControlTokenSize> bytes = random_bytes<kControlTokenSize>(impl_->random);
    ControlToken token(bytes);
    Secret<std::array<u8, kControlTokenSize>> kept(bytes);
    secure_wipe(bytes.data(), bytes.size());

    SecretString env_value = token.env_value();
    impl_->redactor.add_secret(text_bytes(env_value));
    impl_->entries.push_back(Impl::Entry{std::move(key), std::move(kept), std::move(env_value), multiplier, false});
    return token;
}

Result<PeerKey> TokenRegistry::claim(std::span<const u8, kControlTokenSize> token, contracts::game_client::PeerRole role) {
    // Every entry is compared in full, so the time taken does not depend on which one matches.
    Impl::Entry* found = nullptr;
    for (Impl::Entry& entry : impl_->entries) {
        const auto& bytes = entry.bytes.reveal();
        u8 difference = 0;
        for (std::size_t i = 0; i < kControlTokenSize; ++i) difference |= static_cast<u8>(bytes[i] ^ token[i]);
        if (difference == 0) found = &entry;
    }
    if (found == nullptr) return std::unexpected(to_diagnostic(GameChannelError{.code = GameChannelErrorCode::UnknownToken}));
    if (found->key.role != role)
        return std::unexpected(to_diagnostic(GameChannelError{
            .code = GameChannelErrorCode::RoleMismatch, .role = found->key.role, .presented_role = role}));
    if (found->claimed)
        return std::unexpected(to_diagnostic(GameChannelError{
            .code = GameChannelErrorCode::DuplicatePeer, .role = found->key.role, .module = found->key.module}));
    found->claimed = true;
    return found->key;
}

void TokenRegistry::revoke(const PeerKey& key) {
    const auto it = std::ranges::find_if(impl_->entries, [&](const Impl::Entry& entry) { return entry.key == key; });
    if (it == impl_->entries.end()) return;
    impl_->redactor.remove_secret(text_bytes(it->env_value));
    impl_->entries.erase(it);
}

std::optional<std::chrono::milliseconds> TokenRegistry::hello_deadline() const {
    std::optional<RunnerMultiplier> largest;
    for (const Impl::Entry& entry : impl_->entries) {
        if (entry.claimed) continue;
        if (!largest || static_cast<int>(entry.multiplier) > static_cast<int>(*largest)) largest = entry.multiplier;
    }
    if (!largest) return std::nullopt;
    return scaled(kHelloBase, *largest);
}

}  // namespace reboot::game_channel
