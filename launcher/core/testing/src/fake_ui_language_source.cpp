#include "reboot/testing/fake_ui_language_source.hpp"

#include <utility>

namespace rb::testing {

std::vector<std::string> FakeUiLanguageSource::preferred_ui_languages() {
    const std::scoped_lock lock(mutex_);
    ++queries_;
    return languages_;
}

void FakeUiLanguageSource::set(std::vector<std::string> languages) {
    const std::scoped_lock lock(mutex_);
    languages_ = std::move(languages);
}

std::size_t FakeUiLanguageSource::queries() const {
    const std::scoped_lock lock(mutex_);
    return queries_;
}

}  // namespace rb::testing
