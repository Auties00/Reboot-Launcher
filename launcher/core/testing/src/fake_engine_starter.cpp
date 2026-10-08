#include "reboot/testing/fake_engine_starter.hpp"

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

namespace reboot::testing {

Result<ports::StartResult> FakeEngineStarter::ensure_started(const NativePath& engine_exe, const DataRoot& root) {
    Result<ports::StartResult> answer = ports::StartResult::Started;
    {
        const std::scoped_lock lock(mutex_);
        ++calls_;
        if (!answers_.empty()) answer = answers_[std::min(calls_, answers_.size()) - 1];
    }
    // Outside the lock: the hook may bring up an engine that calls back in.
    if (answer && *answer == ports::StartResult::Started && on_started_) on_started_(engine_exe, root);
    return answer;
}

void FakeEngineStarter::script(std::vector<Result<ports::StartResult>> answers) {
    const std::scoped_lock lock(mutex_);
    answers_ = std::move(answers);
    calls_ = 0;
}

void FakeEngineStarter::on_started(UniqueFunction<void(const NativePath& engine_exe, const DataRoot& root)> hook) {
    on_started_ = std::move(hook);
}

std::size_t FakeEngineStarter::calls() const {
    const std::scoped_lock lock(mutex_);
    return calls_;
}

}  // namespace reboot::testing
