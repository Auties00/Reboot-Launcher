#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/win32session/win32_session.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "file_park.hpp"
#include "remote_injector.hpp"
#include "unique_handle.hpp"
#include "wide.hpp"

namespace reboot::os_windows::win32session {
namespace {

using contracts::winhost::InjectPhase;
using contracts::winhost::InjectSpec;
using contracts::winhost::OutputStream;
using contracts::winhost::ProcessRole;
using contracts::winhost::SpawnGame;
namespace wh = contracts::winhost;

// Largest Output payload per read; one pipe read is split into chunks of this size.
constexpr std::size_t kOutputChunk = std::size_t{64} << 10;

std::unexpected<SpawnError> spawn_fail(SpawnStep step, DWORD code) {
    return std::unexpected(SpawnError{step, SystemError{SystemError::Origin::Host, static_cast<i64>(code)}});
}

struct Proc {
    UniqueHandle process;
    UniqueHandle thread;
    u32 pid = 0;
    ProcessRole role{};
    UniqueHandle out_read;  // game only
    UniqueHandle err_read;  // game only
};

class SessionImpl final : public Win32Session {
public:
    explicit SessionImpl(EventSink sink) : sink_(std::move(sink)) {}

    ~SessionImpl() override {
        terminate_and_drain(std::chrono::milliseconds{0});
        parked_.reset();
    }

    std::expected<void, SpawnError> launch(const SpawnGame& spawn);

    std::expected<void, InjectError> inject(const InjectSpec& spec) override {
        RemoteInjector injector(game().process.get());
        auto loaded = injector.load_now(spec);
        if (!loaded) {
            emit(wh::Injected{spec.path_utf16, false, loaded.error().error.code});
            return std::unexpected(loaded.error());
        }
        emit(wh::Injected{spec.path_utf16, true, std::nullopt});
        held_.push_back(std::move(*loaded));
        return {};
    }

    std::expected<void, SpawnError> resume() override {
        if (ResumeThread(game().thread.get()) == static_cast<DWORD>(-1))
            return spawn_fail(SpawnStep::Resume, GetLastError());
        for (auto& deferred : after_resume_) finish_after_resume(deferred);
        after_resume_.clear();
        for (auto& early : early_apc_) confirm_early(early);
        early_apc_.clear();
        return {};
    }

    void stop(std::chrono::milliseconds grace) override {
        terminate_and_drain(grace);
        parked_.reset();
    }

private:
    Proc& game() { return procs_.front(); }

    void emit(SessionEvent event) {
        std::scoped_lock lock(sink_mutex_);
        if (sink_) sink_(std::move(event));
    }

    std::expected<Proc, SpawnError> create_suspended(const Bytes& exe, const std::vector<Bytes>& argv,
                                                     ProcessRole role, bool game_process);

    void finish_after_resume(const InjectSpec& spec) {
        RemoteInjector injector(game().process.get());
        auto loaded = injector.load_now(spec);
        if (!loaded) {
            emit(wh::Injected{spec.path_utf16, false, loaded.error().error.code});
            return;
        }
        emit(wh::Injected{spec.path_utf16, true, std::nullopt});
        held_.push_back(std::move(*loaded));
    }

    void confirm_early(InjectedDll& dll) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        bool ok = false;
        while (std::chrono::steady_clock::now() < deadline) {
            if (module_loaded(game().process.get(), dll.base_name)) {
                ok = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{25});
        }
        if (dll.remote_path != nullptr) {
            VirtualFreeEx(game().process.get(), dll.remote_path, 0, MEM_RELEASE);
            dll.remote_path = nullptr;
        }
        emit(wh::Injected{dll.path_utf16, ok, ok ? std::nullopt : std::optional<i64>{ERROR_MOD_NOT_FOUND}});
        held_.push_back(std::move(dll));
    }

    void start_watchers();
    void terminate_and_drain(std::chrono::milliseconds grace);

    EventSink sink_;
    std::mutex sink_mutex_;
    UniqueHandle job_;
    UniqueHandle stdin_null_;
    std::vector<Proc> procs_;  // index 0 is the game
    std::vector<InjectedDll> held_;
    std::vector<InjectSpec> after_resume_;
    std::vector<InjectedDll> early_apc_;
    std::optional<FilePark> parked_;
    std::vector<std::thread> watchers_;
    Bytes env_block_;
    std::wstring cwd_;
    bool terminated_ = false;
};

std::expected<Proc, SpawnError> SessionImpl::create_suspended(const Bytes& exe, const std::vector<Bytes>& argv,
                                                              ProcessRole role, bool game_process) {
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
        SetHandleInformation(out_read.get(), HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(err_read.get(), HANDLE_FLAG_INHERIT, 0);
    }

    HANDLE child_out = game_process ? out_write.get() : stdin_null_.get();
    HANDLE child_err = game_process ? err_write.get() : stdin_null_.get();
    std::array<HANDLE, 3> inherited{stdin_null_.get(), child_out, child_err};
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
                                  inherited.size() * sizeof(HANDLE), nullptr, nullptr) == 0)
        return spawn_fail(SpawnStep::AttributeList, GetLastError());

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = stdin_null_.get();
    si.StartupInfo.hStdOutput = child_out;
    si.StartupInfo.hStdError = child_err;
    si.lpAttributeList = attr_list;

    std::wstring command_line = build_command_line(exe, argv);
    const std::wstring cwd = cwd_;
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(
        nullptr, command_line.data(), nullptr, nullptr, TRUE,
        CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT, env_block_.data(),
        cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi);
    if (ok == 0)
        return spawn_fail(game_process ? SpawnStep::CreateGame : SpawnStep::CreateCompanion, GetLastError());

    Proc proc;
    proc.process = UniqueHandle(pi.hProcess);
    proc.thread = UniqueHandle(pi.hThread);
    proc.pid = pi.dwProcessId;
    proc.role = role;
    proc.out_read = std::move(out_read);
    proc.err_read = std::move(err_read);
    return proc;
}

void SessionImpl::start_watchers() {
    for (std::size_t i = 0; i < procs_.size(); ++i) {
        Proc& proc = procs_[i];
        if (proc.out_read) {
            HANDLE pipe = proc.out_read.get();
            watchers_.emplace_back([this, pipe] {
                std::array<u8, kOutputChunk> buffer{};
                DWORD read = 0;
                while (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) != 0 &&
                       read != 0)
                    emit(wh::Output{ProcessRole::Game, OutputStream::Stdout, Bytes(buffer.begin(), buffer.begin() + read)});
            });
        }
        if (proc.err_read) {
            HANDLE pipe = proc.err_read.get();
            watchers_.emplace_back([this, pipe] {
                std::array<u8, kOutputChunk> buffer{};
                DWORD read = 0;
                while (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) != 0 &&
                       read != 0)
                    emit(wh::Output{ProcessRole::Game, OutputStream::Stderr, Bytes(buffer.begin(), buffer.begin() + read)});
            });
        }
        HANDLE process = proc.process.get();
        ProcessRole role = proc.role;
        watchers_.emplace_back([this, process, role] {
            WaitForSingleObject(process, INFINITE);
            DWORD code = 0;
            const bool got = GetExitCodeProcess(process, &code) != 0;
            emit(wh::Exited{role, got ? std::optional<i64>{static_cast<i64>(code)} : std::nullopt});
        });
    }
}

void SessionImpl::terminate_and_drain(std::chrono::milliseconds grace) {
    if (!terminated_ && job_) {
        if (grace.count() > 0) {
            const auto deadline = std::chrono::steady_clock::now() + grace;
            for (;;) {
                JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
                if (QueryInformationJobObject(job_.get(), JobObjectBasicAccountingInformation, &info, sizeof(info),
                                              nullptr) != 0 &&
                    info.ActiveProcesses == 0)
                    break;
                if (std::chrono::steady_clock::now() >= deadline) break;
                std::this_thread::sleep_for(std::chrono::milliseconds{20});
            }
        }
        TerminateJobObject(job_.get(), 1);
        for (;;) {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
            if (QueryInformationJobObject(job_.get(), JobObjectBasicAccountingInformation, &info, sizeof(info),
                                          nullptr) == 0 ||
                info.ActiveProcesses == 0)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        terminated_ = true;
    }
    for (auto& watcher : watchers_)
        if (watcher.joinable()) watcher.join();
    watchers_.clear();
}

std::expected<void, SpawnError> SessionImpl::launch(const SpawnGame& spawn) {
    env_block_ = spawn.env_block_utf16;
    if (env_block_.empty() || env_block_.back() != 0) env_block_.insert(env_block_.end(), {0, 0});
    cwd_ = to_wide(spawn.cwd_utf16);

    parked_.emplace(spawn.park_utf16);

    HANDLE raw_job = CreateJobObjectW(nullptr, nullptr);
    if (raw_job == nullptr) return spawn_fail(SpawnStep::CreateJob, GetLastError());
    job_ = UniqueHandle(raw_job);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (SetInformationJobObject(job_.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) == 0)
        return spawn_fail(SpawnStep::ConfigureJob, GetLastError());

    stdin_null_ = UniqueHandle(CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));

    auto game = create_suspended(spawn.exe_utf16, spawn.argv_utf16, ProcessRole::Game, true);
    if (!game) {
        terminate_and_drain(std::chrono::milliseconds{0});
        return std::unexpected(game.error());
    }
    procs_.push_back(std::move(*game));
    emit(wh::Spawned{ProcessRole::Game, procs_.front().pid});

    for (const auto& companion : spawn.companions) {
        auto proc = create_suspended(companion.exe_utf16, companion.argv_utf16, ProcessRole::Companion, false);
        if (!proc) {
            terminate_and_drain(std::chrono::milliseconds{0});
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
                terminate_and_drain(std::chrono::milliseconds{0});
                return spawn_fail(SpawnStep::Inject, static_cast<DWORD>(queued.error().error.code));
            }
            early_apc_.push_back(std::move(*queued));
        } else {
            after_resume_.push_back(entry);
        }
    }

    start_watchers();
    return {};
}

}  // namespace

std::expected<std::unique_ptr<Win32Session>, SpawnError> launch_session(const SpawnGame& spawn, EventSink on_event) {
    auto session = std::make_unique<SessionImpl>(std::move(on_event));
    if (auto launched = session->launch(spawn); !launched) return std::unexpected(launched.error());
    return session;
}

}  // namespace reboot::os_windows::win32session
