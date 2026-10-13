#include "win32.hpp"

#include <objbase.h>
#include <oleauto.h>
#include <taskschd.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include "engine_launch.hpp"
#include "engine_task.hpp"
#include "messages.hpp"
#include "reboot/os_windows/ipc/pipe_trust.hpp"
#include "reboot/os_windows/ipc/windows_caller_context.hpp"
#include "reboot/os_windows/ipc/windows_engine_starter.hpp"
#include "reboot/os_windows/platform/windows_integration_registrar.hpp"
#include "reboot/testing/wall_clock_waiter.hpp"

namespace {

using namespace rb;
using namespace std::chrono_literals;
using rb::os_windows::ipc::TaskRun;
using rb::os_windows::ipc::WindowsEngineStarter;
using rb::os_windows::platform::WindowsIntegrationRegistrar;

// What the fake engine exits with when the task, not the starter, ran it.
constexpr LONG kRanAsTask = 7;

// It registers this user's real engine task, so it runs only where that is allowed, as on CI.
void require_opt_in() {
    std::array<wchar_t, 4> opted_in{};
    if (GetEnvironmentVariableW(L"REBOOT_CONFORMANCE_MUTATES_USER", opted_in.data(), static_cast<DWORD>(opted_in.size())) != 1 ||
        opted_in[0] != L'1') {
        SKIP("set REBOOT_CONFORMANCE_MUTATES_USER=1 to register this user's engine task");
    }
}

template <class T>
struct Released {
    T* ptr = nullptr;
    Released() = default;
    Released(const Released&) = delete;
    Released& operator=(const Released&) = delete;
    ~Released() {
        if (ptr != nullptr) ptr->Release();
    }
};

class Bstr {
public:
    explicit Bstr(const std::wstring& text) : value_(SysAllocStringLen(text.data(), static_cast<UINT>(text.size()))) {}
    ~Bstr() { SysFreeString(value_); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;

    [[nodiscard]] BSTR get() const noexcept { return value_; }

private:
    BSTR value_;
};

struct TaskSnapshot {
    TASK_STATE state = TASK_STATE_UNKNOWN;
    LONG last_result = 0;
};

// Read on this thread in the MTA, as the registrar and the starter do.
[[nodiscard]] std::optional<TaskSnapshot> task_snapshot(const std::string& task_name) {
    struct Apartment {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Apartment() = default;
        Apartment(const Apartment&) = delete;
        Apartment& operator=(const Apartment&) = delete;
        ~Apartment() {
            if (SUCCEEDED(hr)) CoUninitialize();
        }
    } apartment;
    if (FAILED(apartment.hr)) return std::nullopt;
    Released<ITaskService> service;
    if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService,
                                reinterpret_cast<void**>(&service.ptr))))
        return std::nullopt;
    VARIANT empty;
    VariantInit(&empty);
    if (FAILED(service.ptr->Connect(empty, empty, empty, empty))) return std::nullopt;
    Released<ITaskFolder> root;
    if (FAILED(service.ptr->GetFolder(Bstr{L"\\"}.get(), &root.ptr))) return std::nullopt;
    Released<IRegisteredTask> task;
    if (FAILED(root.ptr->GetTask(Bstr{std::wstring(task_name.begin(), task_name.end())}.get(), &task.ptr))) return std::nullopt;
    TaskSnapshot snapshot;
    if (FAILED(task.ptr->get_State(&snapshot.state)) || FAILED(task.ptr->get_LastTaskResult(&snapshot.last_result)))
        return std::nullopt;
    return snapshot;
}

[[nodiscard]] NativePath fake_engine_exe() {
#if defined(REBOOT_FAKE_PIPE_ENGINE_EXE)
    return NativePath{REBOOT_FAKE_PIPE_ENGINE_EXE};
#else
    SKIP("built without reboot-fake-pipe-engine");
    return {};
#endif
}

// The task WindowsIntegrationRegistrar registers for the fake engine, removed when this goes.
struct EngineTask {
    std::string user_sid;
    NativePath engine_exe = fake_engine_exe();
    // Only an entry under the fake's own directory reads as Ours, so remove() never takes a real one.
    WindowsIntegrationRegistrar registrar{engine_exe.parent_path(), user_sid};
    std::string name = os_windows::ipc::engine_task_name(user_sid);
    // A task found in place, even a stale one, is left alone.
    bool registered = false;

    ~EngineTask() {
        if (registered) (void)registrar.remove(ports::IntegrationKind::EngineAgent);
    }
};

[[nodiscard]] std::string current_user_sid() {
    auto trust = os_windows::ipc::PipeTrust::for_current_process();
    REQUIRE(trust);
    return trust->user_sid();
}

// Never clobbers a task a real install registered.
void register_task(EngineTask& task) {
    CHECK(task.name == os_windows::platform::engine_task_name(task.user_sid));
    const auto before = task.registrar.status(ports::IntegrationKind::EngineAgent);
    REQUIRE(before);
    if (before->state != ports::IntegrationState::Absent) SKIP("this user already has an engine task: " << before->detail);
    task.registered = true;
    REQUIRE(task.registrar.apply(ports::IntegrationKind::EngineAgent, task.engine_exe));
    const auto after = task.registrar.status(ports::IntegrationKind::EngineAgent);
    REQUIRE(after);
    CHECK(after->state == ports::IntegrationState::Ours);
}

[[nodiscard]] std::string string_arg(const Diagnostic& diag, std::string_view name) {
    const Arg* arg = diag.find_arg(name);
    if (arg == nullptr) return {};
    const auto* text = std::get_if<std::string>(arg);
    return text != nullptr ? *text : std::string{};
}

}  // namespace

TEST_CASE("the registered engine task starts the engine in the caller's session", "[engine_task]") {
    require_opt_in();
    auto probe = os_windows::ipc::WindowsCallerContext::detect();
    REQUIRE(probe);
    const ports::CallerContext caller = probe->capture();
    const std::optional<u32> session = os_windows::ipc::parse_session_id(caller.os_session);
    if (!caller.interactive || !session) SKIP("the task runs with an interactive token; session " << caller.os_session << " has none");
    EngineTask task{current_user_sid()};
    register_task(task);

    const auto first =
        os_windows::ipc::run_engine_task(task.name, task.engine_exe, *session, WindowsEngineStarter::kTaskSchedulerDeadline);
    REQUIRE(first);
    CHECK(*first == TaskRun::Started);
    testing::WallClockWaiter waiter{50ms};
    CHECK(waiter.wait_until(
        [&] {
            const auto now = task_snapshot(task.name);
            return now && now->state == TASK_STATE_RUNNING;
        },
        10s));
    const auto second =
        os_windows::ipc::run_engine_task(task.name, task.engine_exe, *session, WindowsEngineStarter::kTaskSchedulerDeadline);
    REQUIRE(second);
    CHECK(*second == TaskRun::Running);

    // Exit 7 shows the task ran the fake with the engine's on-demand arguments.
    std::optional<TaskSnapshot> ended;
    CHECK(waiter.wait_until(
        [&] {
            ended = task_snapshot(task.name);
            return ended && ended->state != TASK_STATE_RUNNING;
        },
        15s));
    REQUIRE(ended);
    CHECK(ended->last_result == kRanAsTask);
}

TEST_CASE("a Task Scheduler call hung past the deadline is cancelled and nothing runs after it", "[engine_task]") {
    require_opt_in();
    EngineTask task{current_user_sid()};
    register_task(task);
    DWORD session = 0;
    REQUIRE(ProcessIdToSessionId(GetCurrentProcessId(), &session));

    // Shared with the task thread, which run_engine_task may leave behind.
    struct Hang {
        std::mutex mutex;
        std::condition_variable changed;
        bool released = false;
        std::vector<std::string> calls;
    };
    auto hang = std::make_shared<Hang>();
    const auto began = std::chrono::steady_clock::now();
    const auto run = os_windows::ipc::run_engine_task(
        task.name, task.engine_exe, session, WindowsEngineStarter::kTaskSchedulerDeadline, [hang](std::string_view call) {
            std::unique_lock lock{hang->mutex};
            hang->calls.emplace_back(call);
            if (call == "ITaskFolder::GetTask") hang->changed.wait(lock, [&] { return hang->released; });
        });
    const auto took = std::chrono::steady_clock::now() - began;
    {
        const std::lock_guard lock{hang->mutex};
        hang->released = true;
    }
    hang->changed.notify_all();

    REQUIRE_FALSE(run);
    CHECK(run.error().is(os_windows::ipc::kTaskSchedulerTimedOut));
    CHECK(string_arg(run.error(), "call") == "ITaskFolder::GetTask");
    CHECK(took >= WindowsEngineStarter::kTaskSchedulerDeadline);
    // The deadline, the cancel's one second of grace and the 50 ms watch interval.
    CHECK(took < WindowsEngineStarter::kTaskSchedulerDeadline + 2s);

    // The released GetTask answers late, and no call after it starts.
    std::this_thread::sleep_for(1s);
    {
        const std::lock_guard lock{hang->mutex};
        const std::vector<std::string> expected{"CoCreateInstance(TaskScheduler)", "ITaskService::Connect",
                                                "ITaskService::GetFolder", "ITaskFolder::GetTask"};
        CHECK(hang->calls == expected);
    }
    const auto snapshot = task_snapshot(task.name);
    REQUIRE(snapshot);
    CHECK(snapshot->state != TASK_STATE_RUNNING);
    CHECK(snapshot->last_result == SCHED_S_TASK_HAS_NOT_RUN);
}
