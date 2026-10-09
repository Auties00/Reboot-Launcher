#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/job_process_launcher.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "file_ops.hpp"
#include "process_token.hpp"
#include "reboot/foundation/log.hpp"
#include "unique_handle.hpp"
#include "wide.hpp"
#include "win_error.hpp"

namespace rb::os_windows::platform {

namespace {

using namespace std::chrono_literals;

constexpr std::size_t kReadChunk = 64 * 1024;
// A process outside our reach that inherited a pipe end would otherwise hold back the exit.
constexpr std::chrono::milliseconds kOutputDrainBound = 2s;

using Bytes = std::vector<u8>;
using OutputCallback = UniqueFunction<void(std::span<const u8>)>;

// What the child's threads share with it; a thread left running past the child keeps it alive.
struct ChildState {
    // Serialises every callback, so output always lands before the exit.
    std::mutex output_mutex;
    std::array<OutputCallback, 2> on_output;
    std::array<Bytes, 2> held_output;
    UniqueFunction<void(ports::ChildExit)> on_exit;
    std::optional<ports::ChildExit> held_exit;
    bool closed = false;
    // The thread inside a callback, which may destroy the child from there.
    std::atomic<std::thread::id> delivering{};

    std::mutex readers_mutex;
    std::condition_variable readers_done;
    int readers_running = 0;

    std::mutex writer_mutex;
    std::condition_variable writer_wake;
    std::deque<Bytes> outbox;
    bool stdin_closing = false;
    bool stopping = false;

    // Runs `call` with output_mutex held by the caller.
    template <class Call>
    void as_deliverer(Call&& call) {
        struct Scope {
            std::atomic<std::thread::id>& id;
            ~Scope() { id.store(std::thread::id{}, std::memory_order_release); }
        } scope{delivering};
        delivering.store(std::this_thread::get_id(), std::memory_order_release);
        call();
    }

    void deliver_output(std::size_t stream, std::span<const u8> bytes) {
        std::scoped_lock lock(output_mutex);
        if (closed) return;
        if (on_output[stream]) {
            as_deliverer([&] { on_output[stream](bytes); });
        } else {
            held_output[stream].insert(held_output[stream].end(), bytes.begin(), bytes.end());
        }
    }

    void deliver_exit(ports::ChildExit exit) {
        std::scoped_lock lock(output_mutex);
        if (closed) return;
        if (on_exit) {
            as_deliverer([&] { on_exit(exit); });
            on_exit = nullptr;
        } else {
            held_exit = exit;
        }
    }
};

void guarded(std::string_view where, UniqueFunction<void()> body) noexcept {
    try {
        body();
    } catch (...) {
        REBOOT_LOG_ERROR(Engine, "internal.bug: {} threw", where);
    }
}

void read_loop(const std::shared_ptr<ChildState>& state, UniqueHandle pipe, std::size_t stream) {
    guarded("child output reader", [&] {
        std::vector<u8> buffer(kReadChunk);
        DWORD got = 0;
        while (ReadFile(pipe.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr) != 0 && got != 0)
            state->deliver_output(stream, std::span<const u8>(buffer.data(), got));
    });
    std::scoped_lock lock(state->readers_mutex);
    --state->readers_running;
    state->readers_done.notify_all();
}

void write_loop(const std::shared_ptr<ChildState>& state, UniqueHandle pipe) {
    guarded("child stdin writer", [&] {
        for (;;) {
            Bytes next;
            {
                std::unique_lock lock(state->writer_mutex);
                state->writer_wake.wait(lock, [&] { return state->stopping || state->stdin_closing || !state->outbox.empty(); });
                if (state->stopping || state->outbox.empty()) return;
                next = std::move(state->outbox.front());
                state->outbox.pop_front();
            }
            std::span<const u8> rest(next);
            while (!rest.empty()) {
                DWORD written = 0;
                if (WriteFile(pipe.get(), rest.data(), static_cast<DWORD>(rest.size()), &written, nullptr) == 0) return;
                rest = rest.subspan(written);
            }
        }
    });
}

void wait_loop(const std::shared_ptr<ChildState>& state, UniqueHandle process) {
    guarded("child exit waiter", [&] {
        WaitForSingleObject(process.get(), INFINITE);
        {
            std::unique_lock lock(state->readers_mutex);
            state->readers_done.wait_for(lock, kOutputDrainBound, [&] { return state->readers_running == 0; });
        }
        DWORD code = 0;
        ports::ChildExit exit;
        if (GetExitCodeProcess(process.get(), &code) != 0) exit.code = static_cast<int>(code);
        state->deliver_exit(exit);
    });
}

[[nodiscard]] Result<std::chrono::system_clock::time_point> creation_time(HANDLE process) {
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetProcessTimes(process, &created, &exited, &kernel, &user) == 0)
        return std::unexpected(call_failed("GetProcessTimes", GetLastError()));
    return to_time_point(created);
}

// nullopt when `pid` is gone or is not the process created at `created`.
[[nodiscard]] Result<std::optional<UniqueHandle>> open_recorded(u32 pid, std::chrono::system_clock::time_point created,
                                                               DWORD access) {
    UniqueHandle process(OpenProcess(access | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
    if (!process) {
        const DWORD error = GetLastError();
        // Gone, or another user's: either way not a child we recorded.
        if (error == ERROR_INVALID_PARAMETER || error == ERROR_ACCESS_DENIED) return std::optional<UniqueHandle>{};
        return std::unexpected(call_failed("OpenProcess", error));
    }
    auto started = creation_time(process.get());
    if (!started) return std::unexpected(std::move(started.error()));
    if (!same_creation_time(*started, created)) return std::optional<UniqueHandle>{};
    return std::optional<UniqueHandle>{std::move(process)};
}

class JobChild final : public ports::ChildProcess {
public:
    JobChild(UniqueHandle job, u32 pid, std::chrono::system_clock::time_point created, std::shared_ptr<ChildState> state)
        : job_(std::move(job)), pid_(pid), created_(created), state_(std::move(state)) {}

    ~JobChild() override {
        // A callback ending its own child already holds output_mutex, and the other threads may
        // wait on it, so they are left to finish against the shared state instead of joined.
        const bool from_callback = state_->delivering.load(std::memory_order_acquire) == std::this_thread::get_id();
        {
            std::unique_lock lock(state_->output_mutex, std::defer_lock);
            if (!from_callback) lock.lock();
            state_->closed = true;
            // The running callback is released with the shared state.
            if (!from_callback) {
                state_->on_output = {};
                state_->on_exit = nullptr;
            }
        }
        TerminateJobObject(job_.get(), 1);
        {
            std::scoped_lock lock(state_->writer_mutex);
            state_->stopping = true;
        }
        state_->writer_wake.notify_all();
        for (std::thread& thread : threads_) {
            if (!thread.joinable()) continue;
            if (from_callback) {
                thread.detach();
            } else {
                // The killed job closes the far ends, so every read and write returns.
                thread.join();
            }
        }
    }

    void start(std::optional<UniqueHandle> stdin_write, std::array<std::optional<UniqueHandle>, 2> output_reads,
               UniqueHandle process) {
        auto state = state_;
        for (std::size_t stream = 0; stream < output_reads.size(); ++stream) {
            if (!output_reads[stream]) continue;
            {
                std::scoped_lock lock(state->readers_mutex);
                ++state->readers_running;
            }
            threads_.emplace_back(
                [state, pipe = std::move(*output_reads[stream]), stream]() mutable { read_loop(state, std::move(pipe), stream); });
        }
        if (stdin_write) {
            has_stdin_ = true;
            threads_.emplace_back([state, pipe = std::move(*stdin_write)]() mutable { write_loop(state, std::move(pipe)); });
        }
        threads_.emplace_back([state, process = std::move(process)]() mutable { wait_loop(state, std::move(process)); });
    }

    [[nodiscard]] u32 pid() const override { return pid_; }
    [[nodiscard]] std::chrono::system_clock::time_point created() const override { return created_; }

    void write_stdin(std::span<const u8> bytes) override {
        if (!has_stdin_ || bytes.empty()) return;
        {
            std::scoped_lock lock(state_->writer_mutex);
            if (state_->stdin_closing || state_->stopping) return;
            state_->outbox.emplace_back(bytes.begin(), bytes.end());
        }
        state_->writer_wake.notify_one();
    }

    void close_stdin() override {
        {
            std::scoped_lock lock(state_->writer_mutex);
            state_->stdin_closing = true;
        }
        state_->writer_wake.notify_one();
    }

    void on_stdout(UniqueFunction<void(std::span<const u8>)> callback) override { set_output(0, std::move(callback)); }
    void on_stderr(UniqueFunction<void(std::span<const u8>)> callback) override { set_output(1, std::move(callback)); }

    // A held exit is delivered here, and its callback may destroy this child, so only `state` is
    // touched once it ran.
    void on_exit(UniqueFunction<void(ports::ChildExit)> callback) override {
        const std::shared_ptr<ChildState> state = state_;
        std::scoped_lock lock(state->output_mutex);
        if (state->closed) return;
        if (state->held_exit) {
            const ports::ChildExit exit = *std::exchange(state->held_exit, std::nullopt);
            state->as_deliverer([&] { callback(exit); });
            return;
        }
        state->on_exit = std::move(callback);
    }

    Result<void> terminate_tree() override {
        if (TerminateJobObject(job_.get(), 1) == 0) return std::unexpected(call_failed("TerminateJobObject", GetLastError()));
        return {};
    }

private:
    // Output that arrived before the callback is handed over first, in order.
    void set_output(std::size_t stream, OutputCallback callback) {
        const std::shared_ptr<ChildState> state = state_;
        std::scoped_lock lock(state->output_mutex);
        if (state->closed) return;
        if (!state->held_output[stream].empty() && callback) {
            const Bytes held = std::exchange(state->held_output[stream], {});
            state->as_deliverer([&] { callback(held); });
            if (state->closed) return;
        }
        state->on_output[stream] = std::move(callback);
    }

    UniqueHandle job_;
    u32 pid_ = 0;
    std::chrono::system_clock::time_point created_;
    std::shared_ptr<ChildState> state_;
    std::vector<std::thread> threads_;
    bool has_stdin_ = false;
};

struct Pipe {
    UniqueHandle read;
    UniqueHandle write;
};

// The end the child gets is inheritable; ours is not.
[[nodiscard]] Result<Pipe> make_pipe(bool child_reads) {
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE read = nullptr;
    HANDLE write = nullptr;
    if (CreatePipe(&read, &write, &inherit, 0) == 0) return std::unexpected(call_failed("CreatePipe", GetLastError()));
    Pipe pipe{UniqueHandle(read), UniqueHandle(write)};
    HANDLE ours = child_reads ? pipe.write.get() : pipe.read.get();
    if (SetHandleInformation(ours, HANDLE_FLAG_INHERIT, 0) == 0)
        return std::unexpected(call_failed("SetHandleInformation", GetLastError()));
    return pipe;
}

}  // namespace

Result<std::unique_ptr<ports::ChildProcess>> JobProcessLauncher::spawn(const ports::ProcessLaunch& launch) {
    auto base = user_environment();
    if (!base) return std::unexpected(std::move(base.error()));
    std::wstring environment = build_environment_block(*base, launch.env.vars);

    UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    if (!job) return std::unexpected(call_failed("CreateJobObjectW", GetLastError()));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    if (SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof limits) == 0)
        return std::unexpected(call_failed("SetInformationJobObject", GetLastError()));

    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    UniqueHandle null_device(CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                                         OPEN_EXISTING, 0, nullptr));
    if (!null_device) return std::unexpected(call_failed("CreateFileW", GetLastError(), NativePath(L"NUL")));

    std::optional<UniqueHandle> stdin_write;
    std::array<std::optional<UniqueHandle>, 2> output_reads;
    std::array<UniqueHandle, 3> child_ends;
    if (launch.stdio == ports::StdioMode::ControlChannel) {
        auto in = make_pipe(true);
        if (!in) return std::unexpected(std::move(in.error()));
        child_ends[0] = std::move(in->read);
        stdin_write = std::move(in->write);
    }
    if (launch.stdio != ports::StdioMode::Null) {
        for (std::size_t stream = 0; stream < 2; ++stream) {
            auto out = make_pipe(false);
            if (!out) return std::unexpected(std::move(out.error()));
            child_ends[stream + 1] = std::move(out->write);
            output_reads[stream] = std::move(out->read);
        }
    }
    const auto end_or_null = [&](std::size_t index) { return child_ends[index] ? child_ends[index].get() : null_device.get(); };
    std::array<HANDLE, 3> stdio{end_or_null(0), end_or_null(1), end_or_null(2)};
    std::vector<HANDLE> inherited;
    for (HANDLE handle : stdio)
        if (std::ranges::find(inherited, handle) == inherited.end()) inherited.push_back(handle);

    SIZE_T attribute_size = 0;
    InitializeProcThreadAttributeList(nullptr, 2, 0, &attribute_size);
    std::vector<u8> attribute_storage(attribute_size);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
    if (InitializeProcThreadAttributeList(attributes, 2, 0, &attribute_size) == 0)
        return std::unexpected(call_failed("InitializeProcThreadAttributeList", GetLastError()));
    struct AttributeGuard {
        LPPROC_THREAD_ATTRIBUTE_LIST list;
        ~AttributeGuard() { DeleteProcThreadAttributeList(list); }
    } attribute_guard{attributes};
    HANDLE job_handle = job.get();
    if (UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &job_handle, sizeof job_handle, nullptr,
                                  nullptr) == 0 ||
        UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited.data(),
                                  inherited.size() * sizeof(HANDLE), nullptr, nullptr) == 0)
        return std::unexpected(call_failed("UpdateProcThreadAttribute", GetLastError()));

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof startup;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = stdio[0];
    startup.StartupInfo.hStdOutput = stdio[1];
    startup.StartupInfo.hStdError = stdio[2];
    startup.lpAttributeList = attributes;

    const std::wstring application = shell_path(launch.exe);
    std::wstring command_line = build_command_line(NativePath(application), launch.args);
    const std::wstring directory = shell_path(launch.cwd.empty() ? launch.exe.parent_path() : launch.cwd);
    DWORD flags = CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW;
    if (launch.own_group) flags |= CREATE_NEW_PROCESS_GROUP;
    PROCESS_INFORMATION info{};
    if (CreateProcessW(application.c_str(), command_line.data(), nullptr, nullptr, TRUE, flags, environment.data(),
                       directory.c_str(), &startup.StartupInfo, &info) == 0)
        return std::unexpected(call_failed("CreateProcessW", GetLastError(), launch.exe));
    UniqueHandle process(info.hProcess);
    CloseHandle(info.hThread);
    // The child holds its own ends now; ours would keep the pipes from reaching EOF.
    for (UniqueHandle& end : child_ends) end.reset();

    auto created = creation_time(process.get());
    if (!created) {
        TerminateJobObject(job.get(), 1);
        return std::unexpected(std::move(created.error()));
    }
    auto child = std::make_unique<JobChild>(std::move(job), info.dwProcessId, *created, std::make_shared<ChildState>());
    try {
        child->start(std::move(stdin_write), std::move(output_reads), std::move(process));
    } catch (...) {
        return std::unexpected(call_failed("CreateThread", ERROR_NOT_ENOUGH_MEMORY, launch.exe));
    }
    return std::unique_ptr<ports::ChildProcess>(std::move(child));
}

Result<bool> JobProcessLauncher::is_alive(u32 pid, std::chrono::system_clock::time_point created) {
    auto process = open_recorded(pid, created, 0);
    if (!process) return std::unexpected(std::move(process.error()));
    if (!*process) return false;
    return WaitForSingleObject((*process)->get(), 0) == WAIT_TIMEOUT;
}

Result<void> JobProcessLauncher::kill(u32 pid, std::chrono::system_clock::time_point created) {
    auto process = open_recorded(pid, created, PROCESS_TERMINATE);
    if (!process) return std::unexpected(std::move(process.error()));
    if (!*process || WaitForSingleObject((*process)->get(), 0) != WAIT_TIMEOUT) return {};
    if (TerminateProcess((*process)->get(), 1) == 0) return std::unexpected(call_failed("TerminateProcess", GetLastError()));
    return {};
}

}  // namespace rb::os_windows::platform
