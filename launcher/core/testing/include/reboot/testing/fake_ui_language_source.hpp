#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

#include "reboot/ports/ui_language_source.hpp"

namespace rb::testing {

// Covers no capability ids (decision testing-strategy).
// IUiLanguageSource answering the list the test set, as an OS whose user picked those languages.
class FakeUiLanguageSource final : public ports::IUiLanguageSource {
public:
    [[nodiscard]] std::vector<std::string> preferred_ui_languages() override;

    void set(std::vector<std::string> languages);
    [[nodiscard]] std::size_t queries() const;

private:
    mutable std::mutex mutex_;
    std::vector<std::string> languages_;
    std::size_t queries_ = 0;
};

}  // namespace rb::testing
