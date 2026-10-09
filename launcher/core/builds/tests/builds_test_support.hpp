#pragma once

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "reboot/builds/build_layout.hpp"
#include "reboot/builds/build_usage.hpp"
#include "reboot/catalog/catalog_service.hpp"
#include "reboot/catalog/catalog_source.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "reboot/testing/scratch_dir.hpp"

namespace reboot::builds::test {

// The strand beside real worker threads: they post from their threads, timed tasks follow the
// ManualClock, and only the test thread runs anything.
class TestStrand final : public Executor {
public:
    explicit TestStrand(ManualClock& clock) : clock_(clock) {}

    void post(UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        ready_.push_back(std::move(task));
        posted_.notify_one();
    }
    void post_at(SteadyTime when, UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        timed_.emplace(when, std::move(task));
    }

    std::size_t run_ready() {
        std::size_t ran = 0;
        while (UniqueFunction<void()> task = next(false)) {
            task();
            ++ran;
        }
        return ran;
    }

    // Runs tasks, waiting for other threads to post, until `done` holds.
    template <class Done>
    void run_until(Done&& done) {
        run_ready();
        while (!done()) {
            UniqueFunction<void()> task = next(true);
            REQUIRE(static_cast<bool>(task));
            task();
            run_ready();
        }
    }

    // Moves time one due task at a time, so each timer runs at its own deadline.
    void advance(std::chrono::steady_clock::duration by) {
        const SteadyTime end = clock_.steady_now() + by;
        run_ready();
        while (true) {
            SteadyTime due{};
            {
                const std::scoped_lock lock(mutex_);
                if (timed_.empty() || timed_.begin()->first > end) break;
                due = timed_.begin()->first;
            }
            if (due > clock_.steady_now()) clock_.advance(due - clock_.steady_now());
            run_ready();
        }
        if (end > clock_.steady_now()) clock_.advance(end - clock_.steady_now());
        run_ready();
    }

private:
    UniqueFunction<void()> next(bool wait) {
        std::unique_lock lock(mutex_);
        const SteadyTime now = clock_.steady_now();
        while (!timed_.empty() && timed_.begin()->first <= now) {
            ready_.push_back(std::move(timed_.begin()->second));
            timed_.erase(timed_.begin());
        }
        // A bound, not a sleep: a missing post fails the test instead of hanging it.
        if (wait && !posted_.wait_for(lock, std::chrono::seconds{30}, [this] { return !ready_.empty(); })) return {};
        if (ready_.empty()) return {};
        UniqueFunction<void()> task = std::move(ready_.front());
        ready_.pop_front();
        return task;
    }

    ManualClock& clock_;
    std::mutex mutex_;
    std::condition_variable posted_;
    std::deque<UniqueFunction<void()>> ready_;
    std::multimap<SteadyTime, UniqueFunction<void()>> timed_;
};

// InMemoryFileSystem for documents, but remove_tree on the real disk, where build trees live.
class TreeFileSystem final : public ports::IFileSystem {
public:
    // A sidecar beside a real staged file goes to memory, under the same folder.
    Result<void> atomic_replace(const NativePath& target, std::span<const u8> bytes, bool keep_backup) override {
        std::error_code ec;
        if (std::filesystem::is_directory(target.parent_path(), ec)) memory.make_dir(target.parent_path());
        return memory.atomic_replace(target, bytes, keep_backup);
    }
    Result<std::vector<u8>> read_all(const NativePath& path) override { return memory.read_all(path); }
    Result<ports::FileLock> lock_exclusive(const NativePath& path, bool wait) override {
        return memory.lock_exclusive(path, wait);
    }
    Result<void> restrict_to_owner(const NativePath& path) override { return memory.restrict_to_owner(path); }
    Result<ports::HeldFile> open_deny_write(const NativePath& path) override { return memory.open_deny_write(path); }
    // A build tree on disk counts as present, as the shell's trash checks before it moves anything.
    Result<ports::FileRevision> revision(const NativePath& path) override {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) return ports::FileRevision{};
        return memory.revision(path);
    }
    Result<ports::SharedRead> read_shared(const NativePath& path, u64 offset, std::size_t max_bytes) override {
        return memory.read_shared(path, offset, max_bytes);
    }
    Result<void> create_dirs_owner_only(const NativePath& path) override { return memory.create_dirs_owner_only(path); }
    Result<void> remove_tree(const NativePath& path) override {
        {
            const std::scoped_lock lock(mutex);
            removed.push_back(path);
            if (fail_removal) return std::unexpected(internal_bug("test_remove_tree"));
        }
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        if (ec) return std::unexpected(internal_bug("test_remove_tree"));
        return {};
    }

    testing::InMemoryFileSystem memory;
    std::mutex mutex;
    std::vector<NativePath> removed;
    bool fail_removal = false;
};

class FakeBuildUsage final : public IBuildUsage {
public:
    [[nodiscard]] std::vector<SessionId> sessions_using(BuildId build) const override {
        const auto it = sessions.find(build);
        return it == sessions.end() ? std::vector<SessionId>{} : it->second;
    }
    void stop_sessions_using(BuildId build, UniqueFunction<void(Result<void>)> done) override {
        stop_requests.push_back(build);
        if (stop_result) sessions.erase(build);
        done(stop_result);
    }

    std::map<BuildId, std::vector<SessionId>> sessions;
    std::vector<BuildId> stop_requests;
    Result<void> stop_result;
};

// Answers every load at once with the same catalog.
class FixedCatalogSource final : public catalog::ICatalogSource {
public:
    explicit FixedCatalogSource(catalog::Catalog catalog, catalog::CatalogOrigin origin)
        : catalog_(std::move(catalog)), origin_(origin) {}

    void load(catalog::CatalogFetch, CancelToken, UniqueFunction<void(catalog::CatalogLoadResult)> done) override {
        done(catalog::LoadedCatalog{.catalog = catalog_, .origin = origin_, .warnings = {}});
    }

private:
    catalog::Catalog catalog_;
    catalog::CatalogOrigin origin_;
};

[[nodiscard]] inline GameVersion version(std::string_view text) {
    auto parsed = GameVersion::parse(text);
    REQUIRE(parsed);
    return *parsed;
}

[[nodiscard]] inline testing::ScratchDir make_scratch(std::string_view prefix) {
    testing::FakeRandom random(42);
    Result<testing::ScratchDir> dir = testing::ScratchDir::create(random, prefix);
    REQUIRE(dir);
    return std::move(*dir);
}

inline void write_file(const NativePath& path, std::span<const u8> bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(out.good());
}

inline void write_text(const NativePath& path, std::string_view text) {
    write_file(path, std::span<const u8>(reinterpret_cast<const u8*>(text.data()), text.size()));
}

[[nodiscard]] inline std::vector<u8> read_file(const NativePath& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

[[nodiscard]] inline std::vector<u8> utf16le(std::string_view ascii) {
    std::vector<u8> out;
    for (const char c : ascii) {
        out.push_back(static_cast<u8>(c));
        out.push_back(0);
    }
    return out;
}

inline void put16(std::vector<u8>& out, std::size_t at, u32 value) {
    out[at] = static_cast<u8>(value);
    out[at + 1] = static_cast<u8>(value >> 8);
}

inline void put32(std::vector<u8>& out, std::size_t at, u32 value) {
    put16(out, at, value & 0xFFFF);
    put16(out, at + 2, value >> 16);
}

// A PE32+ image whose one .rsrc section holds `version` as the RT_VERSION resource, so the reader
// walks the three directory levels the way it does on a real executable.
[[nodiscard]] inline std::vector<u8> make_pe(std::span<const u8> version, std::span<const u8> trailer = {}) {
    constexpr std::size_t kNt = 0x40;
    constexpr std::size_t kOptional = kNt + 24;
    constexpr std::size_t kOptionalSize = 240;
    constexpr std::size_t kSections = kOptional + kOptionalSize;
    constexpr std::size_t kRaw = 0x200;
    constexpr u32 kRva = 0x1000;
    constexpr std::size_t kBlob = 88;

    const std::size_t resource_size = kBlob + version.size();
    std::vector<u8> pe(kRaw + resource_size, 0);
    pe[0] = 'M';
    pe[1] = 'Z';
    put32(pe, 0x3C, kNt);
    pe[kNt] = 'P';
    pe[kNt + 1] = 'E';
    put16(pe, kNt + 4, 0x8664);
    put16(pe, kNt + 6, 1);
    put16(pe, kNt + 20, kOptionalSize);
    put16(pe, kOptional, 0x20B);
    put32(pe, kOptional + 108, 16);
    put32(pe, kOptional + 112 + 16, kRva);
    put32(pe, kOptional + 112 + 20, static_cast<u32>(resource_size));

    pe[kSections] = '.';
    pe[kSections + 1] = 'r';
    put32(pe, kSections + 8, static_cast<u32>(resource_size));
    put32(pe, kSections + 12, kRva);
    put32(pe, kSections + 16, static_cast<u32>(resource_size));
    put32(pe, kSections + 20, kRaw);

    // Type 16 -> name 1 -> language 0x409 -> data entry -> blob.
    put16(pe, kRaw + 14, 1);
    put32(pe, kRaw + 16, 16);
    put32(pe, kRaw + 20, 0x8000'0000u | 24);
    put16(pe, kRaw + 24 + 14, 1);
    put32(pe, kRaw + 24 + 16, 1);
    put32(pe, kRaw + 24 + 20, 0x8000'0000u | 48);
    put16(pe, kRaw + 48 + 14, 1);
    put32(pe, kRaw + 48 + 16, 0x409);
    put32(pe, kRaw + 48 + 20, 72);
    put32(pe, kRaw + 72, kRva + static_cast<u32>(kBlob));
    put32(pe, kRaw + 76, static_cast<u32>(version.size()));
    std::ranges::copy(version, pe.begin() + static_cast<std::ptrdiff_t>(kRaw + kBlob));
    pe.insert(pe.end(), trailer.begin(), trailer.end());
    return pe;
}

// A minimal version blob around one release string, as a StringFileInfo value would hold it.
[[nodiscard]] inline std::vector<u8> version_blob(std::string_view text) {
    std::vector<u8> blob = utf16le("VS_VERSION_INFO");
    blob.resize(blob.size() + 8, 0);
    const std::vector<u8> value = utf16le(text);
    blob.insert(blob.end(), value.begin(), value.end());
    blob.push_back(0);
    blob.push_back(0);
    return blob;
}

// A build tree: the shipping exe and CrashReportClient carry `crash_report` and `shipping` markers.
inline void write_build(const NativePath& root, std::string_view shipping_text,
                        std::optional<std::string_view> crash_report_text = std::nullopt) {
    const NativePath binaries = root / "FortniteGame" / "Binaries" / "Win64";
    write_file(binaries / std::string(kShippingExe), make_pe(version_blob(shipping_text)));
    write_text(binaries / std::string(kLauncherExe), "launcher");
    if (crash_report_text) {
        write_file(root / "Engine" / "Binaries" / "Win64" / std::string(kCrashReportClientExe),
                   make_pe(version_blob(*crash_report_text)));
    }
}

[[nodiscard]] inline std::string arg_text(const Diagnostic& diag, std::string_view name) {
    const Arg* arg = diag.find_arg(name);
    if (arg == nullptr) return {};
    if (const auto* text = std::get_if<std::string>(arg)) return *text;
    if (const auto* number = std::get_if<u64>(arg)) return std::to_string(*number);
    if (const auto* path = std::get_if<WirePath>(arg)) return path->display;
    return {};
}

}  // namespace reboot::builds::test
