#include "reboot/gameserver/game_server_binary.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "reboot/contracts/game_server.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/gameserver/game_server_error.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/process/line_reader.hpp"
#include "reboot/process/process_spec.hpp"

namespace reboot::gameserver {

namespace {

namespace gs = contracts::game_server;

constexpr std::size_t kHashChunk = std::size_t{64} << 10;

struct Hashed {
    ports::FileRevision revision;
    Sha256Digest sha256{};
};

[[nodiscard]] Diagnostic error_for(const NativePath& exe, GameServerErrorCode code,
                                   std::optional<Diagnostic> cause = std::nullopt) {
    return to_diagnostic(GameServerError{.code = code, .path = exe, .cause = std::move(cause)});
}

// The open handle keeps writers out, so the revision read here is the one hashed.
[[nodiscard]] Result<Hashed> hash_exe(ports::IFileSystem& fs, const NativePath& exe,
                                      const std::optional<Hashed>& previous, const CancelToken& token) {
    Result<ports::HeldFile> held = fs.open_deny_write(exe);
    if (!held) return std::unexpected(std::move(held.error()));
    Result<ports::FileRevision> revision = fs.revision(exe);
    if (!revision) return std::unexpected(std::move(revision.error()));
    if (previous && previous->revision == *revision) return *previous;

    Sha256 hasher;
    std::vector<u8> buffer(kHashChunk);
    while (true) {
        if (token.cancelled()) return std::unexpected(internal_bug("gameserver.hash_cancelled"));
        Result<std::size_t> read = held->read(buffer);
        if (!read) return std::unexpected(std::move(read.error()));
        if (*read == 0) break;
        hasher.update(std::span<const u8>(buffer.data(), *read));
    }
    return Hashed{*revision, hasher.finish()};
}

}  // namespace

Result<NativePath> locate_game_server(const InstallLayout& install, const std::optional<NativePath>& dev_override) {
    const NativePath& exe = dev_override ? *dev_override : install.game_server_exe;
    if (!exe.is_absolute()) return std::unexpected(error_for(exe, GameServerErrorCode::PathNotAbsolute));
    return exe;
}

struct GameServerBinary::Impl {
    // One `--describe` child; kept after a timeout until its exit is seen, so its record is closed.
    struct DescribeRun {
        ~DescribeRun() { alive.cancel(CancelReason::Shutdown); }

        Sha256Digest sha256{};
        std::unique_ptr<ports::ChildProcess> child;
        process::ChildRecord spawned;
        std::vector<u8> output;
        bool overflowed = false;
        std::unique_ptr<process::LineReader> stderr_lines;
        TimerHandle deadline;
        CancelSource alive;
    };

    NativePath exe;
    ports::IFileSystem& fs;
    ports::IProcessLauncher& launcher;
    WorkerPool& workers;
    Executor& strand;
    TimerService& timers;
    const IClock& clock;
    storage::DocumentStore<DescribeCacheDocument>& cache;
    BaseEnvSource base_env;
    process::ChildRecordCallback record;
    std::chrono::milliseconds describe_deadline;

    CancelSource alive;
    bool running = false;
    std::vector<UniqueFunction<void(Result<DescribedBinary>)>> waiters;
    std::optional<Hashed> hashed;
    std::optional<DescribedBinary> current;
    std::unique_ptr<DescribeRun> run;
    std::vector<std::unique_ptr<DescribeRun>> dying;

    ~Impl() {
        alive.cancel(CancelReason::Shutdown);
        if (run) kill(*run);
        for (const std::unique_ptr<DescribeRun>& gone : dying) kill(*gone);
    }

    // A child whose kill failed stays recorded, so the next engine start reaps it.
    void kill(DescribeRun& target) {
        if (target.child && target.child->terminate_tree() && record) record(target.spawned, process::RecordChange::Exited);
    }

    void start() {
        running = true;
        const CancelToken token = alive.token();
        workers.submit<Hashed>(
            [&fs = fs, exe = exe, previous = hashed](CancelToken cancel) { return hash_exe(fs, exe, previous, cancel); },
            token, strand, [this, token](Result<Hashed> result) {
                if (!token.cancelled()) on_hashed(std::move(result));
            });
    }

    void on_hashed(Result<Hashed> result) {
        if (!result) return finish(std::unexpected(error_for(exe, GameServerErrorCode::ExeUnreadable, std::move(result.error()))));
        hashed = *result;
        if (const CachedDescription* cached = cache.get().find(result->sha256)) {
            if (Result<void> valid = check(cached->description); !valid) return finish(std::unexpected(std::move(valid.error())));
            return finish(DescribedBinary{exe, result->sha256, cached->description});
        }
        spawn(result->sha256);
    }

    void spawn(const Sha256Digest& sha256) {
        const auto spawn_failed = [this](Diagnostic cause) {
            finish(std::unexpected(error_for(exe, GameServerErrorCode::DescribeSpawnFailed, std::move(cause))));
        };
        Result<process::BuiltEnv> env = base_env ? base_env() : Result<process::BuiltEnv>(process::BuiltEnv{});
        if (!env) return spawn_failed(std::move(env.error()));
        process::ProcessSpec spec;
        spec.role = process::ChildRole::GameServer;
        spec.exe = exe;
        spec.args = {"--describe"};
        spec.env = std::move(*env);
        spec.stdio = ports::StdioMode::Capture;
        if (Result<void> valid = spec.validate(); !valid) return spawn_failed(std::move(valid.error()));
        Result<std::unique_ptr<ports::ChildProcess>> started = [&] {
            const process::WipingLaunch launch = spec.to_launch();
            return launcher.spawn(launch.get());
        }();
        if (!started) return spawn_failed(std::move(started.error()));

        run = std::make_unique<DescribeRun>();
        DescribeRun* target = run.get();
        target->sha256 = sha256;
        target->child = std::move(*started);
        target->spawned = process::ChildRecord{.pid = target->child->pid(),
                                               .created = target->child->created(),
                                               .role = process::ChildRole::GameServer};
        target->stderr_lines = std::make_unique<process::LineReader>([](std::string_view line, bool) {
            REBOOT_LOG_DEBUG(Host, "game server --describe: {}", line);
        });
        const CancelToken token = target->alive.token();
        Executor* to = &strand;
        target->child->on_stdout([this, target, token, to](std::span<const u8> bytes) {
            to->post([this, target, token, data = std::vector<u8>(bytes.begin(), bytes.end())] {
                if (!token.cancelled()) on_output(*target, data);
            });
        });
        target->child->on_stderr([target, token, to](std::span<const u8> bytes) {
            to->post([target, token, data = std::vector<u8>(bytes.begin(), bytes.end())] {
                if (!token.cancelled()) target->stderr_lines->feed(data);
            });
        });
        target->child->on_exit([this, target, token, to](ports::ChildExit) {
            to->post([this, target, token] {
                if (!token.cancelled()) on_exit(*target);
            });
        });
        target->deadline = timers.after(describe_deadline, [this, target] { on_deadline(*target); });
        if (record) record(target->spawned, process::RecordChange::Spawned);
    }

    void on_output(DescribeRun& target, std::span<const u8> bytes) {
        if (target.overflowed) return;
        // One description frame and its header fit well inside this; more is not a description.
        if (target.output.size() + bytes.size() > kChildFrameCap + 16) {
            target.overflowed = true;
            target.output.clear();
            static_cast<void>(target.child->terminate_tree());
            return;
        }
        target.output.insert(target.output.end(), bytes.begin(), bytes.end());
    }

    void on_deadline(DescribeRun& target) {
        if (&target != run.get()) return;
        static_cast<void>(target.child->terminate_tree());
        dying.push_back(std::move(run));
        finish(std::unexpected(to_diagnostic(GameServerError{
            .code = GameServerErrorCode::DescribeTimeout, .path = exe, .timeout = describe_deadline})));
    }

    void on_exit(DescribeRun& target) {
        target.deadline.cancel();
        target.stderr_lines->finish();
        if (record) record(target.spawned, process::RecordChange::Exited);
        if (&target != run.get()) {
            std::erase_if(dying, [&](const std::unique_ptr<DescribeRun>& gone) { return gone.get() == &target; });
            return;
        }
        std::unique_ptr<DescribeRun> ended = std::move(run);
        Result<GameServerDescription> description = parse(*ended);
        if (!description) return finish(std::unexpected(std::move(description.error())));
        if (Result<void> valid = check(*description); !valid) return finish(std::unexpected(std::move(valid.error())));
        store(ended->sha256, *description);
        finish(DescribedBinary{exe, ended->sha256, std::move(*description)});
    }

    [[nodiscard]] Result<GameServerDescription> parse(const DescribeRun& target) const {
        if (target.overflowed) return std::unexpected(error_for(exe, GameServerErrorCode::DescribeMalformed));
        if (target.output.empty()) return std::unexpected(error_for(exe, GameServerErrorCode::DescribeNoOutput));
        Framer framer(kChildFrameCap);
        std::vector<std::pair<u64, std::vector<u8>>> frames;
        const Framer::Status fed = framer.feed(target.output, [&](const RawFrame& frame) {
            frames.emplace_back(frame.type, std::vector<u8>(frame.payload.begin(), frame.payload.end()));
            return true;
        });
        if (fed != Framer::Status::ok || framer.buffered() != 0 || frames.size() != 1 ||
            frames[0].first != contract_frame_type_v<GameServerDescription>)
            return std::unexpected(error_for(exe, GameServerErrorCode::DescribeMalformed));
        Result<GameServerDescription> description = decode_contract<GameServerDescription>(frames[0].second);
        if (!description)
            return std::unexpected(error_for(exe, GameServerErrorCode::DescribeMalformed, std::move(description.error())));
        // The codec accepts any u8 role; the cache and the port block know only the declared ones.
        const auto unknown_role = [](const gs::SocketSpec& socket) { return socket.role > gs::SocketRole::Beacon; };
        if (std::ranges::any_of(description->sockets, unknown_role))
            return std::unexpected(error_for(exe, GameServerErrorCode::DescribeMalformed));
        return description;
    }

    [[nodiscard]] Result<void> check(const GameServerDescription& description) const {
        if (description.protocol != gs::kGameServerProtocol)
            return std::unexpected(to_diagnostic(GameServerError{.code = GameServerErrorCode::ProtocolMismatch,
                                                                 .path = exe,
                                                                 .expected = gs::kGameServerProtocol,
                                                                 .actual = description.protocol}));
        const auto games = static_cast<u64>(std::ranges::count(description.sockets, gs::SocketRole::Game,
                                                                &gs::SocketSpec::role));
        if (games != 1)
            return std::unexpected(to_diagnostic(
                GameServerError{.code = GameServerErrorCode::InvalidSockets, .path = exe, .actual = games}));
        return {};
    }

    // A cache that cannot be written only costs a describe run next time.
    void store(const Sha256Digest& sha256, const GameServerDescription& description) {
        Result<u64> written = cache.update(
            [entry = CachedDescription{sha256, clock.system_now(), description}](DescribeCacheDocument& document) mutable {
                document.put(std::move(entry));
            });
        if (!written) REBOOT_LOG_WARN(Host, "cannot cache the game server description: {}", written.error().id);
    }

    void finish(Result<DescribedBinary> result) {
        running = false;
        if (result) current = *result;
        std::vector<UniqueFunction<void(Result<DescribedBinary>)>> waiting = std::move(waiters);
        waiters.clear();
        const CancelToken token = alive.token();
        for (UniqueFunction<void(Result<DescribedBinary>)>& done : waiting) {
            if (token.cancelled()) return;
            done(result);
        }
    }
};

GameServerBinary::GameServerBinary(NativePath exe, ports::IFileSystem& fs, ports::IProcessLauncher& launcher,
                                   WorkerPool& workers, Executor& strand, TimerService& timers, const IClock& clock,
                                   storage::DocumentStore<DescribeCacheDocument>& cache, BaseEnvSource base_env,
                                   process::ChildRecordCallback record, std::chrono::milliseconds describe_deadline)
    : impl_(new Impl{.exe = std::move(exe),
                     .fs = fs,
                     .launcher = launcher,
                     .workers = workers,
                     .strand = strand,
                     .timers = timers,
                     .clock = clock,
                     .cache = cache,
                     .base_env = std::move(base_env),
                     .record = std::move(record),
                     .describe_deadline = describe_deadline}) {}

GameServerBinary::~GameServerBinary() = default;

const NativePath& GameServerBinary::exe() const noexcept { return impl_->exe; }

void GameServerBinary::describe(UniqueFunction<void(Result<DescribedBinary>)> done) {
    impl_->waiters.push_back(std::move(done));
    if (!impl_->running) impl_->start();
}

const DescribedBinary* GameServerBinary::current() const noexcept {
    return impl_->current ? &*impl_->current : nullptr;
}

void GameServerBinary::forget(const Sha256Digest& sha256) {
    if (impl_->cache.get().find(sha256) == nullptr) return;
    Result<u64> written = impl_->cache.update([sha256](DescribeCacheDocument& document) { document.erase(sha256); });
    if (!written) REBOOT_LOG_WARN(Host, "cannot drop the cached game server description: {}", written.error().id);
}

}  // namespace reboot::gameserver
