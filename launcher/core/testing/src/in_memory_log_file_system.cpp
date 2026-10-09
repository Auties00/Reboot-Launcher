#include "reboot/testing/in_memory_log_file_system.hpp"

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <span>
#include <utility>

#include "messages.hpp"

namespace rb::testing {
namespace {

struct FileNode {
    std::string bytes;
    std::chrono::system_clock::time_point modified;
    std::size_t handles = 0;
};

// Lexically normal, without the empty element a trailing separator leaves.
[[nodiscard]] NativePath normal(const NativePath& path) {
    NativePath out = path.lexically_normal();
    if (!out.has_filename() && out.has_relative_path()) out = out.parent_path();
    return out;
}

[[nodiscard]] Diagnostic not_found(const NativePath& path) {
    return make_diag(kTestingDomain, msg::kNotFound).arg("path", path).kind(ErrorKind::NotFound);
}

[[nodiscard]] Diagnostic not_a_directory(const NativePath& path) {
    return make_diag(kTestingDomain, msg::kNotADirectory).arg("path", path).kind(ErrorKind::Conflict);
}

[[nodiscard]] Diagnostic is_a_directory(const NativePath& path) {
    return make_diag(kTestingDomain, msg::kIsADirectory).arg("path", path).kind(ErrorKind::Conflict);
}

struct LogFsState {
    explicit LogFsState(const IClock& clock_ref) : clock(clock_ref) {}

    [[nodiscard]] bool dir_exists(const NativePath& path) const {
        return path == path.root_path() || dirs.contains(path);
    }

    [[nodiscard]] Result<void> make_dirs(const NativePath& path) {
        NativePath walked = path.root_path();
        for (const NativePath& part : path.relative_path()) {
            walked /= part;
            if (files.contains(walked)) return std::unexpected(not_a_directory(walked));
            dirs.insert(walked);
        }
        return {};
    }

    const IClock& clock;
    mutable std::mutex mutex;
    std::set<NativePath> dirs;
    std::map<NativePath, std::shared_ptr<FileNode>> files;
    u64 flushes = 0;
    FaultPlan<LogFsOperation> faults;
};

class MemoryLogFile final : public ports::LogFile::Handle {
public:
    MemoryLogFile(std::shared_ptr<LogFsState> fs, std::shared_ptr<FileNode> node)
        : fs_(std::move(fs)), node_(std::move(node)) {}
    ~MemoryLogFile() override {
        const std::scoped_lock lock(fs_->mutex);
        --node_->handles;
    }
    MemoryLogFile(const MemoryLogFile&) = delete;
    MemoryLogFile& operator=(const MemoryLogFile&) = delete;

    Result<void> append(std::span<const u8> bytes) override {
        if (auto fault = fs_->faults.take(LogFsOperation::Append)) return std::unexpected(std::move(*fault));
        const std::scoped_lock lock(fs_->mutex);
        node_->bytes.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        node_->modified = fs_->clock.system_now();
        return {};
    }

    Result<void> flush() override {
        if (auto fault = fs_->faults.take(LogFsOperation::Flush)) return std::unexpected(std::move(*fault));
        const std::scoped_lock lock(fs_->mutex);
        ++fs_->flushes;
        return {};
    }

private:
    std::shared_ptr<LogFsState> fs_;
    std::shared_ptr<FileNode> node_;
};

}  // namespace

struct InMemoryLogFileSystem::Impl : LogFsState {
    using LogFsState::LogFsState;
};

InMemoryLogFileSystem::InMemoryLogFileSystem(const IClock& clock) : impl_(std::make_shared<Impl>(clock)) {}

InMemoryLogFileSystem::~InMemoryLogFileSystem() = default;

Result<void> InMemoryLogFileSystem::create_directories(const NativePath& dir) {
    if (auto fault = impl_->faults.take(LogFsOperation::CreateDirectories)) return std::unexpected(std::move(*fault));
    const std::scoped_lock lock(impl_->mutex);
    return impl_->make_dirs(normal(dir));
}

Result<ports::LogFile> InMemoryLogFileSystem::open_append(const NativePath& path) {
    if (auto fault = impl_->faults.take(LogFsOperation::OpenAppend)) return std::unexpected(std::move(*fault));
    const NativePath file = normal(path);
    const std::scoped_lock lock(impl_->mutex);
    if (impl_->dirs.contains(file)) return std::unexpected(is_a_directory(file));
    if (!impl_->dir_exists(file.parent_path())) return std::unexpected(not_found(file.parent_path()));
    std::shared_ptr<FileNode>& node = impl_->files[file];
    if (!node) {
        node = std::make_shared<FileNode>();
        node->modified = impl_->clock.system_now();
    }
    ++node->handles;
    const u64 size = node->bytes.size();
    return ports::LogFile(std::make_unique<MemoryLogFile>(impl_, node), size);
}

Result<std::vector<ports::LogDirEntry>> InMemoryLogFileSystem::list(const NativePath& dir) {
    if (auto fault = impl_->faults.take(LogFsOperation::List)) return std::unexpected(std::move(*fault));
    const NativePath parent = normal(dir);
    const std::scoped_lock lock(impl_->mutex);
    if (!impl_->dir_exists(parent)) return std::unexpected(not_found(parent));
    std::vector<ports::LogDirEntry> entries;
    for (const auto& [path, node] : impl_->files)
        if (path.parent_path() == parent) entries.push_back({path, node->bytes.size(), node->modified});
    return entries;
}

Result<void> InMemoryLogFileSystem::remove(const NativePath& path) {
    if (auto fault = impl_->faults.take(LogFsOperation::Remove)) return std::unexpected(std::move(*fault));
    const NativePath file = normal(path);
    const std::scoped_lock lock(impl_->mutex);
    if (impl_->files.erase(file) == 0) return std::unexpected(not_found(file));
    return {};
}

void InMemoryLogFileSystem::put(const NativePath& path, std::string_view text,
                                std::chrono::system_clock::time_point modified) {
    const NativePath file = normal(path);
    const std::scoped_lock lock(impl_->mutex);
    (void)impl_->make_dirs(file.parent_path());
    auto node = std::make_shared<FileNode>();
    node->bytes = std::string(text);
    node->modified = modified;
    impl_->files.insert_or_assign(file, std::move(node));
}

std::optional<std::string> InMemoryLogFileSystem::text(const NativePath& path) const {
    const std::scoped_lock lock(impl_->mutex);
    const auto it = impl_->files.find(normal(path));
    if (it == impl_->files.end()) return std::nullopt;
    return it->second->bytes;
}

bool InMemoryLogFileSystem::exists(const NativePath& path) const {
    const NativePath file = normal(path);
    const std::scoped_lock lock(impl_->mutex);
    return impl_->files.contains(file) || impl_->dir_exists(file);
}

bool InMemoryLogFileSystem::is_dir(const NativePath& path) const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->dir_exists(normal(path));
}

std::size_t InMemoryLogFileSystem::open_handles(const NativePath& path) const {
    const std::scoped_lock lock(impl_->mutex);
    const auto it = impl_->files.find(normal(path));
    return it == impl_->files.end() ? 0 : it->second->handles;
}

std::vector<std::string> InMemoryLogFileSystem::file_names(const NativePath& dir) const {
    const NativePath parent = normal(dir);
    const std::scoped_lock lock(impl_->mutex);
    std::vector<std::string> names;
    for (const auto& entry : impl_->files)
        if (entry.first.parent_path() == parent) names.push_back(display_utf8(entry.first.filename()));
    std::ranges::sort(names);
    return names;
}

u64 InMemoryLogFileSystem::flushes() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->flushes;
}

FaultPlan<LogFsOperation>& InMemoryLogFileSystem::faults() noexcept { return impl_->faults; }

}  // namespace rb::testing
