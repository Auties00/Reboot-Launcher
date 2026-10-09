#pragma once

#include <openssl/evp.h>

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/testing/fault_plan.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "reboot/trust/key_ring.hpp"
#include "reboot/trust/signed_document.hpp"
#include "reboot/trust/signed_document_kind.hpp"

namespace rb::components::test {

[[nodiscard]] inline std::vector<u8> bytes_of(std::string_view text) { return {text.begin(), text.end()}; }

[[nodiscard]] inline std::string sha_hex(std::string_view content) { return to_hex(sha256(bytes_of(content))); }

[[nodiscard]] inline std::string arg_text(const Diagnostic& diag, std::string_view name) {
    const Arg* arg = diag.find_arg(name);
    if (arg == nullptr) return {};
    if (const auto* text = std::get_if<std::string>(arg)) return *text;
    if (const auto* number = std::get_if<u64>(arg)) return std::to_string(*number);
    if (const auto* number = std::get_if<i64>(arg)) return std::to_string(*number);
    if (const auto* path = std::get_if<WirePath>(arg)) return path->display;
    if (const auto* version = std::get_if<SemVer>(arg)) return version->to_string();
    return {};
}

struct PkeyDeleter {
    void operator()(EVP_PKEY* key) const noexcept { EVP_PKEY_free(key); }
};
struct MdCtxDeleter {
    void operator()(EVP_MD_CTX* context) const noexcept { EVP_MD_CTX_free(context); }
};

// Signs the way the release pipeline does: the ReleaseManifest context prefix, then the body.
class TestSigner {
public:
    TestSigner() : key_(EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519")) {
        REQUIRE(key_);
        std::size_t size = public_key_.size();
        REQUIRE(EVP_PKEY_get_raw_public_key(key_.get(), public_key_.data(), &size) == 1);
    }

    [[nodiscard]] std::string signature_file(std::string_view body) const {
        const std::string_view prefix = trust::signature_context(trust::SignedDocumentKind::ReleaseManifest);
        std::vector<u8> message(prefix.begin(), prefix.end());
        message.insert(message.end(), body.begin(), body.end());

        const std::unique_ptr<EVP_MD_CTX, MdCtxDeleter> context(EVP_MD_CTX_new());
        REQUIRE(EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key_.get()) == 1);
        trust::Ed25519Signature signature{};
        std::size_t size = signature.size();
        REQUIRE(EVP_DigestSign(context.get(), signature.data(), &size, message.data(), message.size()) == 1);
        return "ed25519 " + trust::key_id_of(public_key_) + " " + to_hex(signature) + "\n";
    }

    [[nodiscard]] trust::KeyRing ring() const {
        return trust::KeyRing(trust::SignedDocumentKind::ReleaseManifest, {public_key_, {}});
    }

private:
    std::unique_ptr<EVP_PKEY, PkeyDeleter> key_;
    trust::Ed25519PublicKey public_key_{};
};

inline constexpr u64 kFarFutureUnixMs = 4'000'000'000'000;

[[nodiscard]] inline std::string remote_json(std::string_view url, std::string_view content) {
    return R"({"urls":[")" + std::string(url) + R"("],"sha256":")" + sha_hex(content) + R"(","size":)" +
           std::to_string(content.size()) + "}";
}

// Builds release manifests for the tests; every artifact names its content so its sha256 and size match.
struct ManifestJson {
    u64 serial = 1;
    u64 expires_unix_ms = kFarFutureUnixMs;
    std::vector<std::string> payloads;
    std::vector<std::string> runtimes;
    std::vector<std::string> apps;
    std::optional<std::string> endpoint;

    ManifestJson& payload(std::string_view version, u16 abi, std::string_view client_dll,
                          std::optional<std::string_view> winhost = std::nullopt) {
        std::string files = R"([{"role":"client_dll",)" + remote_json("https://cdn.test/" + std::string(version) + "/rb_client.dll", client_dll).substr(1);
        if (winhost)
            files += R"(,{"role":"winhost",)" +
                     remote_json("https://cdn.test/" + std::string(version) + "/reboot-winhost.exe", *winhost).substr(1);
        files += "]";
        payloads.push_back(R"({"version":")" + std::string(version) + R"(","payload_abi":)" + std::to_string(abi) +
                           R"(,"files":)" + files + "}");
        return *this;
    }

    ManifestJson& runtime(std::string_view id, std::string_view kind, std::optional<std::string_view> os,
                          std::string_view archive_url, std::string_view archive) {
        std::string out = R"({"id":")" + std::string(id) + R"(","kind":")" + std::string(kind) +
                          R"(","version":"1","archive":)" + remote_json(archive_url, archive);
        if (os) out += R"(,"platform":{"os":")" + std::string(*os) + R"(","arch":"x64"})";
        runtimes.push_back(out + "}");
        return *this;
    }

    [[nodiscard]] std::string text() const {
        const auto join = [](const std::vector<std::string>& items) {
            std::string out = "[";
            for (std::size_t i = 0; i < items.size(); ++i) out += (i == 0 ? "" : ",") + items[i];
            return out + "]";
        };
        std::string out = R"({"schema":1,"serial":)" + std::to_string(serial) + R"(,"expires_unix_ms":)" +
                          std::to_string(expires_unix_ms) + R"(,"apps":)" + join(apps) + R"(,"payloads":)" +
                          join(payloads) + R"(,"runtimes":)" + join(runtimes);
        if (endpoint) out += R"(,"endpoint":)" + *endpoint;
        return out + "}";
    }
};

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
            ++executed_;
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
            ++executed_;
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

    [[nodiscard]] u64 executed() const noexcept { return executed_; }

private:
    UniqueFunction<void()> next(bool wait) {
        std::unique_lock lock(mutex_);
        const SteadyTime now = clock_.steady_now();
        while (!timed_.empty() && timed_.begin()->first <= now) {
            ready_.push_back(std::move(timed_.begin()->second));
            timed_.erase(timed_.begin());
        }
        // A bound, not a sleep: a missing post fails the test instead of hanging it.
        if (wait && !posted_.wait_for(lock, std::chrono::seconds{20}, [this] { return !ready_.empty(); })) return {};
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
    u64 executed_ = 0;
};

inline constexpr MessageId kDiskError{"components_test.disk_error"};

// IFileSystem over a real scratch directory, since the downloader and the store's renames and
// extraction work on real files. Faults use the in-memory fake's operation names.
class DiskFileSystem final : public ports::IFileSystem {
public:
    Result<void> atomic_replace(const NativePath& target, std::span<const u8> bytes, bool) override {
        if (auto fault = faults_.take(testing::FsOperation::AtomicReplace)) return std::unexpected(std::move(*fault));
        NativePath temp = target;
        temp += ".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) return fail(temp, ErrorKind::Generic);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!out) return fail(temp, ErrorKind::Generic);
        }
        std::error_code error;
        std::filesystem::rename(temp, target, error);
        if (error) return fail(target, ErrorKind::Generic);
        return {};
    }

    Result<std::vector<u8>> read_all(const NativePath& path) override {
        if (auto fault = faults_.take(testing::FsOperation::ReadAll)) return std::unexpected(std::move(*fault));
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) return fail(path, ErrorKind::NotFound);
        std::ifstream in(path, std::ios::binary);
        return std::vector<u8>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    Result<ports::FileLock> lock_exclusive(const NativePath&, bool) override { return ports::FileLock{}; }
    Result<void> restrict_to_owner(const NativePath&) override { return {}; }

    Result<ports::HeldFile> open_deny_write(const NativePath& path) override {
        if (auto fault = faults_.take(testing::FsOperation::OpenDenyWrite)) return std::unexpected(std::move(*fault));
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) return fail(path, ErrorKind::NotFound);
        auto handle = std::make_unique<StreamHandle>(path);
        if (!handle->in) return fail(path, ErrorKind::Generic);
        ++opened_;
        return ports::HeldFile(std::move(handle));
    }

    Result<ports::FileRevision> revision(const NativePath& path) override {
        if (auto fault = faults_.take(testing::FsOperation::Revision)) return std::unexpected(std::move(*fault));
        std::error_code error;
        const auto status = std::filesystem::status(path, error);
        if (error || !std::filesystem::exists(status)) return fail(path, ErrorKind::NotFound);
        ports::FileRevision revision;
        if (std::filesystem::is_regular_file(status)) revision.size = std::filesystem::file_size(path, error);
        return revision;
    }

    Result<ports::SharedRead> read_shared(const NativePath& path, u64 offset, std::size_t max_bytes) override {
        auto all = read_all(path);
        if (!all) return std::unexpected(std::move(all.error()));
        ports::SharedRead out;
        out.revision.size = all->size();
        if (offset < all->size()) {
            const std::size_t start = static_cast<std::size_t>(offset);
            const std::size_t count = (std::min)(max_bytes, all->size() - start);
            out.bytes.assign(all->begin() + static_cast<std::ptrdiff_t>(start),
                             all->begin() + static_cast<std::ptrdiff_t>(start + count));
        }
        return out;
    }

    Result<void> create_dirs_owner_only(const NativePath& path) override {
        if (auto fault = faults_.take(testing::FsOperation::CreateDirsOwnerOnly)) return std::unexpected(std::move(*fault));
        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (error) return fail(path, ErrorKind::Generic);
        return {};
    }

    Result<void> remove_tree(const NativePath& path) override {
        if (auto fault = faults_.take(testing::FsOperation::RemoveTree)) return std::unexpected(std::move(*fault));
        std::error_code error;
        std::filesystem::remove_all(path, error);
        if (error) return fail(path, ErrorKind::Generic);
        return {};
    }

    [[nodiscard]] testing::FaultPlan<testing::FsOperation>& faults() noexcept { return faults_; }
    [[nodiscard]] u64 opened() const noexcept { return opened_; }

    [[nodiscard]] static Diagnostic error(const NativePath& path, ErrorKind kind) {
        return make_diag(ErrorDomain::Internal, kDiskError).arg("path", path).kind(kind).build();
    }

private:
    struct StreamHandle final : ports::HeldFile::Handle {
        explicit StreamHandle(const NativePath& path) : in(path, std::ios::binary) {}

        Result<std::size_t> read(std::span<u8> out) override {
            in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
            return static_cast<std::size_t>(in.gcount());
        }

        std::ifstream in;
    };

    [[nodiscard]] static std::unexpected<Diagnostic> fail(const NativePath& path, ErrorKind kind) {
        return std::unexpected(error(path, kind));
    }

    testing::FaultPlan<testing::FsOperation> faults_;
    std::atomic<u64> opened_{0};
};

[[nodiscard]] inline std::string read_text(const NativePath& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

inline void write_text(const NativePath& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

}  // namespace rb::components::test
