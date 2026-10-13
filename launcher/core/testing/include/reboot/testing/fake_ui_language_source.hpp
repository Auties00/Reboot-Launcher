#pragma once

#include <cstddef>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "reboot/ports/ui_language_source.hpp"

namespace rb::testing {

// Covers no capability ids (decision testing-strategy).
// IUiLanguageSource answering the list the test set, as an OS whose user picked those languages.
class FakeUiLanguageSource final : public ports::IUiLanguageSource {
public:
    [[nodiscard]] std::vector<std::string> preferred_ui_languages() override;
    // The same list, whatever the caller; records its display_env.
    [[nodiscard]] std::vector<std::string> caller_ui_languages(
        std::span<const contracts::ipc::EnvVar> display_env) override;

    void set(std::vector<std::string> languages);
    [[nodiscard]] std::size_t queries() const;
    // The display_env of the last caller_ui_languages call.
    [[nodiscard]] std::vector<contracts::ipc::EnvVar> last_display_env() const;

private:
    mutable std::mutex mutex_;
    std::vector<std::string> languages_;
    std::size_t queries_ = 0;
    std::vector<contracts::ipc::EnvVar> last_display_env_;
};

}  // namespace rb::testing
