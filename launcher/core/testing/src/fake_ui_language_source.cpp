#include "reboot/testing/fake_ui_language_source.hpp"

#include <utility>

namespace rb::testing {

std::vector<std::string> FakeUiLanguageSource::preferred_ui_languages() {
    const std::scoped_lock lock(mutex_);
    ++queries_;
    return languages_;
}

std::vector<std::string> FakeUiLanguageSource::caller_ui_languages(
    std::span<const contracts::ipc::EnvVar> display_env) {
    const std::scoped_lock lock(mutex_);
    ++queries_;
    last_display_env_.assign(display_env.begin(), display_env.end());
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

std::vector<contracts::ipc::EnvVar> FakeUiLanguageSource::last_display_env() const {
    const std::scoped_lock lock(mutex_);
    return last_display_env_;
}

}  // namespace rb::testing
