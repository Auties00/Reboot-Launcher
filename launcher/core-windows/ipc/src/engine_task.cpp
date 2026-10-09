#include "engine_task.hpp"

#include "win32.hpp"

#include <objbase.h>
#include <oleauto.h>
#include <taskschd.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include "engine_launch.hpp"
#include "messages.hpp"
#include "reboot/foundation/log.hpp"
#include "wide.hpp"

namespace rb::os_windows::ipc {
namespace {

// How long a cancelled call may take to return before its thread is left behind.
constexpr std::chrono::seconds kCancelGrace{1};
constexpr std::chrono::milliseconds kWatchInterval{50};
// TASK_RUN_USE_SESSION_ID, which not every SDK's taskschd.h names.
constexpr LONG kRunInSession = 0x4;

template <class T>
class ComRef {
public:
    ComRef() = default;
    ~ComRef() {
        if (ptr_ != nullptr) ptr_->Release();
    }
    ComRef(const ComRef&) = delete;
    ComRef& operator=(const ComRef&) = delete;

    T* operator->() const noexcept { return ptr_; }
    // Only on an empty reference.
    [[nodiscard]] T** out() noexcept { return &ptr_; }
    [[nodiscard]] void** out_void() noexcept { return reinterpret_cast<void**>(&ptr_); }

private:
    T* ptr_ = nullptr;
};

class Bstr {
public:
    Bstr() = default;
    explicit Bstr(const std::wstring& text) : value_(SysAllocStringLen(text.data(), static_cast<UINT>(text.size()))) {}
    ~Bstr() { SysFreeString(value_); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;

    [[nodiscard]] BSTR get() const noexcept { return value_; }
    [[nodiscard]] BSTR* out() noexcept { return &value_; }

private:
    BSTR value_ = nullptr;
};

struct Watch {
    std::mutex mutex;
    std::condition_variable changed;
    // The Task Scheduler call in progress; empty between calls.
    std::string call;
    std::chrono::steady_clock::time_point since;
    DWORD thread_id = 0;
    // Past the deadline no further call starts, so a late answer never reaches RunEx.
    bool cancelled = false;
    std::optional<Result<TaskRun>> result;
};

class TaskDriver {
public:
    TaskDriver(Watch& watch, std::string task_name, NativePath engine_exe, u32 session_id)
        : watch_(watch), task_name_(std::move(task_name)), engine_exe_(std::move(engine_exe)), session_id_(session_id) {}

    [[nodiscard]] TaskRun drive() {
        ComRef<ITaskService> service;
        if (!call("CoCreateInstance(TaskScheduler)", [&] {
                return CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService, service.out_void());
            }))
            return TaskRun::Unusable;
        VARIANT empty;
        VariantInit(&empty);
        if (!call("ITaskService::Connect", [&] { return service->Connect(empty, empty, empty, empty); })) return TaskRun::Unusable;
        ComRef<ITaskFolder> root;
        const Bstr root_path{L"\\"};
        if (!call("ITaskService::GetFolder", [&] { return service->GetFolder(root_path.get(), root.out()); }))
            return TaskRun::Unusable;

        ComRef<IRegisteredTask> task;
        const Bstr name{to_wide(task_name_)};
        const HRESULT found = timed("ITaskFolder::GetTask", [&] { return root->GetTask(name.get(), task.out()); });
        if (found == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || found == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) {
            REBOOT_LOG_INFO(Ipc, "no on-demand engine task {} is registered", task_name_);
            return TaskRun::Unusable;
        }
        if (!succeeded("ITaskFolder::GetTask", found)) return TaskRun::Unusable;

        TASK_STATE state = TASK_STATE_UNKNOWN;
        if (!call("IRegisteredTask::get_State", [&] { return task->get_State(&state); })) return TaskRun::Unusable;
        if (state == TASK_STATE_DISABLED) return unusable("it is disabled");
        ComRef<ITaskDefinition> definition;
        if (!call("IRegisteredTask::get_Definition", [&] { return task->get_Definition(definition.out()); }))
            return TaskRun::Unusable;
        ComRef<IPrincipal> principal;
        if (!call("ITaskDefinition::get_Principal", [&] { return definition->get_Principal(principal.out()); }))
            return TaskRun::Unusable;
        TASK_RUNLEVEL_TYPE level = TASK_RUNLEVEL_HIGHEST;
        if (!call("IPrincipal::get_RunLevel", [&] { return principal->get_RunLevel(&level); })) return TaskRun::Unusable;
        if (level != TASK_RUNLEVEL_LUA) return unusable("it runs elevated");
        ComRef<IActionCollection> actions;
        if (!call("ITaskDefinition::get_Actions", [&] { return definition->get_Actions(actions.out()); }))
            return TaskRun::Unusable;
        LONG count = 0;
        if (!call("IActionCollection::get_Count", [&] { return actions->get_Count(&count); })) return TaskRun::Unusable;
        if (count != 1) return unusable("it has more than one action");
        ComRef<IAction> action;
        if (!call("IActionCollection::get_Item", [&] { return actions->get_Item(1, action.out()); })) return TaskRun::Unusable;
        ComRef<IExecAction> exec;
        if (FAILED(action->QueryInterface(IID_IExecAction, exec.out_void()))) return unusable("its action runs no program");
        Bstr path;
        if (!call("IExecAction::get_Path", [&] { return exec->get_Path(path.out()); })) return TaskRun::Unusable;
        if (!action_runs(expanded(path.get()), engine_exe_)) return unusable("it runs another program");

        if (state == TASK_STATE_RUNNING) return TaskRun::Running;
        ComRef<IRunningTask> running;
        if (!call("IRegisteredTask::RunEx", [&] {
                return task->RunEx(empty, kRunInSession, static_cast<LONG>(session_id_), nullptr, running.out());
            }))
            return TaskRun::Unusable;
        return TaskRun::Started;
    }

private:
    // Runs one Task Scheduler call where the deadline watch sees it.
    template <class F>
    [[nodiscard]] HRESULT timed(std::string_view name, F&& body) {
        {
            const std::lock_guard lock{watch_.mutex};
            if (watch_.cancelled) return E_ABORT;
            watch_.call = name;
            watch_.since = std::chrono::steady_clock::now();
        }
        const HRESULT result = body();
        const std::lock_guard lock{watch_.mutex};
        watch_.call.clear();
        return result;
    }

    template <class F>
    [[nodiscard]] bool call(std::string_view name, F&& body) {
        return succeeded(name, timed(name, std::forward<F>(body)));
    }

    [[nodiscard]] bool succeeded(std::string_view name, HRESULT result) const {
        if (SUCCEEDED(result)) return true;
        REBOOT_LOG_WARN(Ipc, "the on-demand engine task cannot be used: {} failed with {:#010x}", name,
                        static_cast<unsigned long>(result));
        return false;
    }

    [[nodiscard]] TaskRun unusable(std::string_view why) const {
        REBOOT_LOG_WARN(Ipc, "the on-demand engine task {} cannot start the engine: {}", task_name_, why);
        return TaskRun::Unusable;
    }

    [[nodiscard]] static std::wstring expanded(BSTR path) {
        const wchar_t* text = path == nullptr ? L"" : path;
        const DWORD size = ExpandEnvironmentStringsW(text, nullptr, 0);
        if (size == 0) return text;
        std::wstring result(size, L'\0');
        const DWORD written = ExpandEnvironmentStringsW(text, result.data(), size);
        if (written == 0 || written > size) return text;
        result.resize(written - 1);
        return result;
    }

    Watch& watch_;
    std::string task_name_;
    NativePath engine_exe_;
    u32 session_id_;
};

void run_on_mta(const std::shared_ptr<Watch>& watch, const std::string& task_name, const NativePath& engine_exe,
                u32 session_id) noexcept {
    Result<TaskRun> result = TaskRun::Unusable;
    try {
        const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(init)) {
            REBOOT_LOG_WARN(Ipc, "the on-demand engine task cannot be used: CoInitializeEx failed with {:#010x}",
                            static_cast<unsigned long>(init));
        } else {
            CoEnableCallCancellation(nullptr);
            result = TaskDriver{*watch, task_name, engine_exe, session_id}.drive();
            CoDisableCallCancellation(nullptr);
            CoUninitialize();
        }
    } catch (...) {
        result = std::unexpected(internal_bug("os_windows.engine_task"));
    }
    const std::lock_guard lock{watch->mutex};
    watch->result = std::move(result);
    watch->changed.notify_all();
}

[[nodiscard]] Diagnostic timed_out(std::string_view call, std::chrono::milliseconds deadline) {
    return make_diag(ErrorDomain::Platform, kTaskSchedulerTimedOut).arg("call", call).arg("deadline", deadline).build();
}

}  // namespace

Result<TaskRun> run_engine_task(const std::string& task_name, const NativePath& engine_exe, u32 session_id,
                                std::chrono::milliseconds call_deadline) {
    auto watch = std::make_shared<Watch>();
    std::thread worker;
    try {
        worker = std::thread([watch, task_name, engine_exe, session_id] {
            {
                const std::lock_guard lock{watch->mutex};
                watch->thread_id = GetCurrentThreadId();
            }
            run_on_mta(watch, task_name, engine_exe, session_id);
        });
    } catch (const std::system_error&) {
        return std::unexpected(internal_bug("os_windows.engine_task.thread"));
    }

    std::unique_lock lock{watch->mutex};
    std::optional<std::string> cancelled;
    std::chrono::steady_clock::time_point cancelled_at;
    while (!watch->result) {
        watch->changed.wait_for(lock, kWatchInterval);
        if (watch->result) break;
        const auto now = std::chrono::steady_clock::now();
        if (!cancelled && !watch->call.empty() && now - watch->since >= call_deadline) {
            cancelled = watch->call;
            cancelled_at = now;
            watch->cancelled = true;
            const DWORD thread_id = watch->thread_id;
            lock.unlock();
            CoCancelCall(thread_id, 0);
            lock.lock();
        } else if (cancelled && now - cancelled_at >= kCancelGrace) {
            // The call ignored the cancel; the thread finishes on its own and only touches `watch`.
            lock.unlock();
            worker.detach();
            return std::unexpected(timed_out(*cancelled, call_deadline));
        }
    }
    Result<TaskRun> result = std::move(*watch->result);
    lock.unlock();
    worker.join();
    if (cancelled) return std::unexpected(timed_out(*cancelled, call_deadline));
    return result;
}

}  // namespace rb::os_windows::ipc
