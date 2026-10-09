#include "reboot/logging/retention_policy.hpp"

#include <algorithm>

namespace rb::logging {

std::vector<NativePath> select_expired(std::span<const LogFileInfo> files, const RetentionPolicy& policy,
                                       std::chrono::system_clock::time_point now,
                                       std::span<const NativePath> in_use) {
    std::vector<const LogFileInfo*> by_age;
    by_age.reserve(files.size());
    for (const LogFileInfo& file : files) by_age.push_back(&file);
    std::ranges::sort(by_age, [](const LogFileInfo* a, const LogFileInfo* b) {
        return a->modified != b->modified ? a->modified < b->modified : a->path < b->path;
    });

    std::vector<LogFileGroup> newest_groups;
    for (const LogFileInfo& file : files)
        if (file.group && std::ranges::find(newest_groups, *file.group) == newest_groups.end())
            newest_groups.push_back(*file.group);
    std::ranges::sort(newest_groups, [](const LogFileGroup& a, const LogFileGroup& b) {
        return a.started_at != b.started_at ? a.started_at > b.started_at : a.pid > b.pid;
    });
    if (newest_groups.size() > policy.max_sessions) newest_groups.resize(policy.max_sessions);

    const auto is_in_use = [&](const LogFileInfo& file) {
        return std::ranges::find(in_use, file.path) != in_use.end();
    };
    const auto over_limits = [&](const LogFileInfo& file) {
        if (now - file.modified > policy.max_age) return true;
        return file.group && std::ranges::find(newest_groups, *file.group) == newest_groups.end();
    };

    std::vector<bool> expired(by_age.size(), false);
    u64 kept_bytes = 0;
    for (std::size_t i = 0; i < by_age.size(); ++i) {
        expired[i] = over_limits(*by_age[i]) && !is_in_use(*by_age[i]);
        if (!expired[i]) kept_bytes += by_age[i]->size;
    }
    for (std::size_t i = 0; i < by_age.size() && kept_bytes > policy.max_total_bytes; ++i) {
        if (expired[i] || is_in_use(*by_age[i])) continue;
        expired[i] = true;
        kept_bytes -= by_age[i]->size;
    }

    std::vector<NativePath> selected;
    for (std::size_t i = 0; i < by_age.size(); ++i)
        if (expired[i]) selected.push_back(by_age[i]->path);
    return selected;
}

}  // namespace rb::logging
