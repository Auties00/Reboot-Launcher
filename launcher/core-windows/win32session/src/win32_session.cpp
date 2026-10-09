#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/win32session/win32_session.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "file_park.hpp"
#include "handle_list.hpp"
#include "poll_until.hpp"
#include "remote_injector.hpp"
#include "unique_handle.hpp"
#include "wide.hpp"

namespace rb::os_windows::win32session {
namespace {

using contracts::winhost::InjectPhase;
using contracts::winhost::InjectSpec;
using contracts::winhost::OutputStream;
using contracts::winhost::ProcessRole;
using contracts::winhost::SpawnGame;
namespace wh = contracts::winhost;

using namespace std::chrono_literals;

// Largest Output payload per read; one pipe read is split into chunks of this size.
constexpr std::size_t kOutputChunk = std::size_t{64} << 10;
// How often a wait on the Job or on the watcher threads polls.
constexpr std::chrono::milliseconds kDrainStep = 10ms;
// How often the module list is checked while an early-bird load is confirmed.
constexpr std::chrono::milliseconds kConfirmStep = 25ms;
// The bound used when SpawnGame gives no timeout: long enough for a kill to take effect, short
// enough never to hang the engine on a wedged process.
constexpr std::chrono::milliseconds kDefaultBound = 5s;

std::unexpected<SpawnError> spawn_fail(SpawnStep step, DWORD code) {
    return std::unexpected(SpawnError{step, SystemError{SystemError::Origin::Host, static_cast<i64>(code)}});
}

std::chrono::milliseconds from_ms(u32 value, std::chrono::milliseconds fallback) {
    return value == 0 ? fallback : std::chrono::milliseconds{value};
}

void sleep_for(std::chrono::milliseconds wait) { std::this_thread::sleep_for(wait); }

std::chrono::steady_clock::time_point steady_now() { return std::chrono::steady_clock::now(); }

std::vector<u32> job_pids(HANDLE job) {
    // Doubles the list until every assigned process fits; capped so a runaway spawner cannot make
    // this allocate without bound.
    for (DWORD capacity = 64; capacity <= (1u << 16); capacity *= 2) {
        const std::size_t bytes = sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + (capacity - 1) * sizeof(ULONG_PTR);
        std::vector<u8> storage(bytes);
        auto* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(storage.data());
        if (QueryInformationJobObject(job, JobObjectBasicProcessIdList, list, static_cast<DWORD>(bytes), nullptr) ==
            0) {
            if (GetLastError() == ERROR_MORE_DATA) continue;
            return {};
        }
        if (list->NumberOfAssignedProcesses > list->NumberOfProcessIdsInList && capacity < (1u << 16)) continue;
        std::vector<u32> pids;
        pids.reserve(list->NumberOfProcessIdsInList);
        for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i)
            pids.push_back(static_cast<u32>(list->ProcessIdList[i]));
        return pids;
    }
    return {};
}

// What the session shares with its watcher threads. A thread left running after a failed kill
// keeps this alive and finds the sink cleared, so it never reaches the session or the caller.
struct Shared {
    std::mutex mutex;
    EventSink sink;
    std::atomic<bool> stopping{false};
    UniqueHandle stop_event;  // manual-reset; ends the exit waits

    void emit(SessionEvent event) {
        std::scoped_lock lock(mutex);
        if (sink) sink(std::move(event));
    }
};

template <class Body>
DWORD WINAPI thread_main(LPVOID param) {
    std::unique_ptr<Body> body(static_cast<Body*>(param));
    // A throwing sink or a failed allocation ends this watcher, never the process.
    try {
        (*body)();
    } catch (...) {
    }
    return 0;
}

// A Win32 thread rather than std::thread: stopping one takes its handle for CancelSynchronousIo
// and a timed wait.
template <class Body>
std::expected<UniqueHandle, DWORD> start_thread(Body body) {
    auto boxed = std::make_unique<Body>(std::move(body));
    HANDLE thread = CreateThread(nullptr, 0, &thread_main<Body>, boxed.get(), 0, nullptr);
    if (thread == nullptr) return std::unexpected(GetLastError());
    static_cast<void>(boxed.release());
    return UniqueHandle(thread);
}

struct Proc {
    UniqueHandle process;
    UniqueHandle thread;
    u32 pid = 0;
    ProcessRole role{};
    UniqueHandle out_read;  // game only; moved to its reader thread
    UniqueHandle err_read;  // game only; moved to its reader thread
};

class SessionImpl final : public Win32Session {
public:
    explicit SessionImpl(EventSink sink) : shared_(std::make_shared<Shared>()) { shared_->sink = std::move(sink); }

    ~SessionImpl() override {
        drop();
        parked_.reset();
    }

    std::expected<void, SpawnError> launch(const SpawnGame& spawn);

    std::expected<void, InjectError> inject(const InjectSpec& spec) override { return load_held(spec); }

    std::expected<void, SpawnError> resume() override {
        if (ResumeThread(game().thread.get()) == static_cast<DWORD>(-1)) {
            const DWORD code = GetLastError();
            abandon();
            return spawn_fail(SpawnStep::Resume, code);
        }
        for (const auto& deferred : after_resume_)
            if (auto done = load_held(deferred); !done) {
                abandon();
                return spawn_fail(SpawnStep::Inject, static_cast<DWORD>(done.error().error.code));
            }
        after_resume_.clear();
        for (auto& early : early_apc_)
            if (auto done = confirm_early(early); !done) {
                abandon();
                return spawn_fail(SpawnStep::Inject, static_cast<DWORD>(done.error().error.code));
            }
        early_apc_.clear();
        return {};
    }

    std::expected<void, StuckProcesses> stop(std::chrono::milliseconds grace) override {
        auto stuck = terminate_and_drain(grace);
        parked_.reset();
        return stuck;
    }

private:
    Proc& game() { return procs_.front(); }

    void emit(SessionEvent event) { shared_->emit(std::move(event)); }

    std::expected<Proc, SpawnError> create_suspended(const Bytes& exe, const std::vector<Bytes>& argv,
                                                     const Bytes& env, ProcessRole role, bool game_process);

    // A waited remote load. The deny-write handle is held whenever the file passed its hash, since
    // a load that timed out may still happen.
    std::expected<void, InjectError> load_held(const InjectSpec& spec) {
        InjectedDll dll;
        auto loaded = RemoteInjector(game().process.get()).load_now(spec, inject_timeout_, dll);
        if (dll.file) held_.push_back(std::move(dll));
        emit(wh::Injected{spec.path_utf16, loaded.has_value(),
                          loaded ? std::nullopt : std::optional<i64>{loaded.error().error.code}});
        return loaded;
    }

    std::expected<void, InjectError> confirm_early(InjectedDll& dll) {
        const bool ok = poll_until([&] { return module_loaded(game().process.get(), dll.base_name); }, sleep_for,
                                   steady_now, inject_timeout_, kConfirmStep);
        // An unconfirmed APC may still run and read the path, so its buffer stays until the Job dies.
        if (ok && dll.remote_path != nullptr) {
            VirtualFreeEx(game().process.get(), dll.remote_path, 0, MEM_RELEASE);
            dll.remote_path = nullptr;
            dll.remote_size = 0;
        }
        emit(wh::Injected{dll.path_utf16, ok, ok ? std::nullopt : std::optional<i64>{ERROR_MOD_NOT_FOUND}});
        held_.push_back(std::move(dll));
        if (!ok)
            return std::unexpected(
                InjectError{InjectStep::Confirm, SystemError{SystemError::Origin::Host, ERROR_MOD_NOT_FOUND}});
        return {};
    }

    std::expected<void, SpawnError> start_watchers();
    void stop_watchers(bool stuck);
    std::expected<void, StuckProcesses> terminate_and_drain(std::chrono::milliseconds grace);
    // Teardown and rollback paths kill the Job and move on; a stuck process there is unactionable.
    void drop() { static_cast<void>(terminate_and_drain(0ms)); }
    void abandon() {
        after_resume_.clear();
        early_apc_.clear();
        drop();
    }

    std::shared_ptr<Shared> shared_;
    UniqueHandle job_;
    UniqueHandle stdin_null_;
    std::vector<Proc> procs_;  // index 0 is the game
    std::vector<InjectedDll> held_;
    std::vector<InjectSpec> after_resume_;
    std::vector<InjectedDll> early_apc_;
    std::optional<FilePark> parked_;
    std::vector<UniqueHandle> watchers_;
    std::wstring cwd_;
    std::chrono::milliseconds inject_timeout_ = kDefaultBound;
    std::chrono::milliseconds drain_timeout_ = kDefaultBound;
    bool terminated_ = false;
};

std::expected<Proc, SpawnError> SessionImpl::create_suspended(const Bytes& exe, const std::vector<Bytes>& argv,
                                                              const Bytes& env, ProcessRole role, bool game_process) {
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};

    UniqueHandle out_read;
    UniqueHandle out_write;
    UniqueHandle err_read;
    UniqueHandle err_write;
    if (game_process) {
        HANDLE r = nullptr;
        HANDLE w = nullptr;
        if (CreatePipe(&r, &w, &inherit, 0) == 0) return spawn_fail(SpawnStep::CreatePipe, GetLastError());
        out_read = UniqueHandle(r);
        out_write = UniqueHandle(w);
        if (CreatePipe(&r, &w, &inherit, 0) == 0) return spawn_fail(SpawnStep::CreatePipe, GetLastError());
        err_read = UniqueHandle(r);
        err_write = UniqueHandle(w);
        // Only the child inherits the write ends; our read ends stay out of the child.
        SetHandleInformation(out_read.get(), HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(err_read.get(), HANDLE_FLAG_INHERIT, 0);
    }

    HANDLE child_out = game_process ? out_write.get() : stdin_null_.get();
    HANDLE child_err = game_process ? err_write.get() : stdin_null_.get();
    std::array<HANDLE, 3> inherited{stdin_null_.get(), child_out, child_err};
    const std::size_t inherited_count = unique_handles(inherited);
    HANDLE job = job_.get();

    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(nullptr, 2, 0, &attr_size);
    std::vector<u8> attr_storage(attr_size);
    auto* attr_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_storage.data());
    if (InitializeProcThreadAttributeList(attr_list, 2, 0, &attr_size) == 0)
        return spawn_fail(SpawnStep::AttributeList, GetLastError());
    struct AttrGuard {
        LPPROC_THREAD_ATTRIBUTE_LIST list;
        ~AttrGuard() { DeleteProcThreadAttributeList(list); }
    } attr_guard{attr_list};
    if (UpdateProcThreadAttribute(attr_list, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &job, sizeof(job), nullptr,
                                  nullptr) == 0)
        return spawn_fail(SpawnStep::AttributeList, GetLastError());
    if (UpdateProcThreadAttribute(attr_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited.data(),
                                  inherited_count * sizeof(HANDLE), nullptr, nullptr) == 0)
        return spawn_fail(SpawnStep::AttributeList, GetLastError());

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = stdin_null_.get();
    si.StartupInfo.hStdOutput = child_out;
    si.StartupInfo.hStdError = child_err;
    si.lpAttributeList = attr_list;

    // Naming the application means the first command-line token is never searched for on a path.
    const std::wstring application = to_wide(exe);
    std::wstring command_line = build_command_line(exe, argv);
    Bytes env_copy;
    if (!env.empty()) {
        // Room for the terminator up front, so padding never reallocates and strands a copy.
        env_copy.reserve(env.size() + 6);
        env_copy.assign(env.begin(), env.end());
        terminate_env_block(env_copy);
    }
    void* env_ptr = env_copy.empty() ? nullptr : env_copy.data();
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(application.c_str(), command_line.data(), nullptr, nullptr, TRUE,
                                   CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
                                   env_ptr, cwd_.empty() ? nullptr : cwd_.c_str(), &si.StartupInfo, &pi);
    const DWORD create_error = ok == 0 ? GetLastError() : 0;
    wipe(command_line);
    wipe(env_copy);
    if (ok == 0)
        return spawn_fail(game_process ? SpawnStep::CreateGame : SpawnStep::CreateCompanion, create_error);

    Proc proc;
    proc.process = UniqueHandle(pi.hProcess);
    proc.thread = UniqueHandle(pi.hThread);
    proc.pid = pi.dwProcessId;
    proc.role = role;
    proc.out_read = std::move(out_read);
    proc.err_read = std::move(err_read);
    return proc;
}

std::expected<void, SpawnError> SessionImpl::start_watchers() {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr) return spawn_fail(SpawnStep::Watch, GetLastError());
    shared_->stop_event = UniqueHandle(event);

    // Each thread owns its handles and a reference to the shared state, so one left running after a
    // failed kill never touches the session. A half-started set is stopped by the caller's rollback.
    try {
        for (Proc& proc : procs_) {
            const ProcessRole role = proc.role;
            for (auto [pipe, stream] : {std::pair{&proc.out_read, OutputStream::Stdout},
                                        std::pair{&proc.err_read, OutputStream::Stderr}}) {
                if (!*pipe) continue;
                auto started = start_thread([shared = shared_, read_end = std::move(*pipe), role, stream] {
                    std::vector<u8> buffer(kOutputChunk);
                    DWORD read = 0;
                    while (!shared->stopping.load(std::memory_order_acquire) &&
                           ReadFile(read_end.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                                    nullptr) != 0 &&
                           read != 0)
                        shared->emit(wh::Output{role, stream, Bytes(buffer.begin(), buffer.begin() + read)});
                });
                if (!started) return spawn_fail(SpawnStep::Watch, started.error());
                watchers_.push_back(std::move(*started));
            }

            HANDLE process = nullptr;
            if (DuplicateHandle(GetCurrentProcess(), proc.process.get(), GetCurrentProcess(), &process,
                                SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0) == 0)
                return spawn_fail(SpawnStep::Watch, GetLastError());
            auto started = start_thread([shared = shared_, process = UniqueHandle(process), role] {
                const std::array<HANDLE, 2> waits{process.get(), shared->stop_event.get()};
                // The stop event ends the wait on a process the kill could not end; no Exited then.
                if (WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, INFINITE) !=
                    WAIT_OBJECT_0)
                    return;
                DWORD code = 0;
                const bool got = GetExitCodeProcess(process.get(), &code) != 0;
                shared->emit(wh::Exited{role, got ? std::optional<i64>{static_cast<i64>(code)} : std::nullopt});
            });
            if (!started) return spawn_fail(SpawnStep::Watch, started.error());
            watchers_.push_back(std::move(*started));
        }
    } catch (...) {
        return spawn_fail(SpawnStep::Watch, ERROR_NOT_ENOUGH_MEMORY);
    }
    return {};
}

void SessionImpl::stop_watchers(bool stuck) {
    if (watchers_.empty()) return;
    SetEvent(shared_->stop_event.get());
    const auto all_done = [&] {
        for (const auto& watcher : watchers_)
            if (WaitForSingleObject(watcher.get(), 0) != WAIT_OBJECT_0) return false;
        return true;
    };
    // With the Job empty a reader reaches EOF on its own after the last output; a stuck process,
    // or one outside the Job that inherited a write end, keeps it blocked until it is cancelled.
    bool done = poll_until(all_done, sleep_for, steady_now, stuck ? 0ms : drain_timeout_, kDrainStep);
    if (!done) {
        shared_->stopping.store(true, std::memory_order_release);
        // Repeated: a reader that read the flag just before it was set is not in ReadFile yet.
        done = poll_until(
            all_done,
            [&](std::chrono::milliseconds wait) {
                for (const auto& watcher : watchers_) CancelSynchronousIo(watcher.get());
                sleep_for(wait);
            },
            steady_now, drain_timeout_, kDrainStep);
    }
    if (!done) {
        std::scoped_lock lock(shared_->mutex);
        shared_->sink = nullptr;
    }
    watchers_.clear();
}

std::expected<void, StuckProcesses> SessionImpl::terminate_and_drain(std::chrono::milliseconds grace) {
    std::optional<StuckProcesses> stuck;
    if (!terminated_ && job_) {
        // The game is the only resumed process, so grace waits on it; the suspended companions
        // would never exit on their own and must not hold up the kill.
        if (grace.count() > 0 && !procs_.empty()) WaitForSingleObject(game().process.get(), wait_millis(grace));
        TerminateJobObject(job_.get(), 1);
        const bool empty = poll_until(
            [&] {
                // The Job's count drops before a killed process is signalled; an exit wait woken by
                // the stop event first would lose that process's Exited.
                for (const Proc& proc : procs_)
                    if (WaitForSingleObject(proc.process.get(), 0) != WAIT_OBJECT_0) return false;
                JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
                return QueryInformationJobObject(job_.get(), JobObjectBasicAccountingInformation, &info,
                                                 sizeof(info), nullptr) != 0 &&
                       info.ActiveProcesses == 0;
            },
            sleep_for, steady_now, drain_timeout_, kDrainStep);
        if (!empty) stuck = StuckProcesses{job_pids(job_.get())};
        terminated_ = true;
    }
    stop_watchers(stuck.has_value());
    if (stuck) return std::unexpected(std::move(*stuck));
    return {};
}

std::expected<void, SpawnError> SessionImpl::launch(const SpawnGame& spawn) {
    cwd_ = to_wide(spawn.cwd_utf16);
    inject_timeout_ = from_ms(spawn.inject_timeout_ms, kDefaultBound);
    drain_timeout_ = from_ms(spawn.drain_timeout_ms, kDefaultBound);

    parked_.emplace(spawn.park_utf16);

    HANDLE raw_job = CreateJobObjectW(nullptr, nullptr);
    if (raw_job == nullptr) return spawn_fail(SpawnStep::CreateJob, GetLastError());
    job_ = UniqueHandle(raw_job);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (SetInformationJobObject(job_.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) == 0)
        return spawn_fail(SpawnStep::ConfigureJob, GetLastError());

    // Inheritable so it can go in each child's handle list as its stdin and the companions' stdio.
    SECURITY_ATTRIBUTES null_inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    stdin_null_ = UniqueHandle(CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE, &null_inherit, OPEN_EXISTING, 0,
                                           nullptr));
    if (!stdin_null_) return spawn_fail(SpawnStep::ConfigureJob, GetLastError());

    auto game = create_suspended(spawn.exe_utf16, spawn.argv_utf16, spawn.env_block_utf16, ProcessRole::Game, true);
    if (!game) {
        drop();
        return std::unexpected(game.error());
    }
    procs_.push_back(std::move(*game));
    emit(wh::Spawned{ProcessRole::Game, procs_.front().pid});

    for (const auto& companion : spawn.companions) {
        // Companions take the caller's environment, not the game's channel layer.
        auto proc = create_suspended(companion.exe_utf16, companion.argv_utf16, Bytes{}, ProcessRole::Companion,
                                     false);
        if (!proc) {
            drop();
            return std::unexpected(proc.error());
        }
        const u32 pid = proc->pid;
        procs_.push_back(std::move(*proc));
        emit(wh::Spawned{ProcessRole::Companion, pid});
    }

    // Early entries: queue APC entries now so they load as the main thread resumes; a straight
    // remote-thread load must wait for the loader, so AfterResume entries are deferred to resume().
    RemoteInjector injector(procs_.front().process.get());
    for (const auto& entry : spawn.inject) {
        if (entry.phase != InjectPhase::Early) continue;
        if (entry.strategy == wh::BootStrategy::EarlyBirdApc) {
            auto queued = injector.queue_early(entry, procs_.front().thread.get());
            if (!queued) {
                emit(wh::Injected{entry.path_utf16, false, queued.error().error.code});
                drop();
                return spawn_fail(SpawnStep::Inject, static_cast<DWORD>(queued.error().error.code));
            }
            early_apc_.push_back(std::move(*queued));
        } else {
            after_resume_.push_back(entry);
        }
    }

    if (auto watched = start_watchers(); !watched) {
        drop();
        return std::unexpected(watched.error());
    }
    return {};
}

}  // namespace

std::expected<std::unique_ptr<Win32Session>, SpawnError> launch_session(const SpawnGame& spawn, EventSink on_event) {
    auto session = std::make_unique<SessionImpl>(std::move(on_event));
    if (auto launched = session->launch(spawn); !launched) return std::unexpected(launched.error());
    return session;
}

}  // namespace rb::os_windows::win32session
