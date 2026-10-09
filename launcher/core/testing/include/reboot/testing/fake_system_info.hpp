#pragma once

#include <mutex>
#include <optional>
#include <string>

#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/os_services.hpp"

namespace rb::testing {

struct FakeSystemFacts {
    ports::OsInfo os{"test", "1.0", "0", "x86_64"};
    bool elevated = false;
    std::string os_session = "1";
    bool under_steam_reaper = false;
    std::optional<NativePath> ca_bundle;
};

// Covers no capability ids (decision testing-strategy).
// ISystemInfo answering the facts the test set, e.g. an elevated engine or a Steam reaper ancestor.
class FakeSystemInfo final : public ports::ISystemInfo {
public:
    [[nodiscard]] ports::OsInfo os() const override;
    [[nodiscard]] bool elevated() const override;
    [[nodiscard]] std::string os_session() const override;
    [[nodiscard]] bool under_steam_reaper() const override;
    [[nodiscard]] std::optional<NativePath> ca_bundle() const override;

    void set(FakeSystemFacts facts);

private:
    mutable std::mutex mutex_;
    FakeSystemFacts facts_;
};

}  // namespace rb::testing
