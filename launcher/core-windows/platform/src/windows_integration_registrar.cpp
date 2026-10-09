#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/windows_integration_registrar.hpp"

#include <filesystem>
#include <system_error>
#include <vector>

#include "com_thread.hpp"
#include "messages.hpp"
#include "registrar_rules.hpp"
#include "unique_handle.hpp"
#include "wide.hpp"
#include "win_error.hpp"

namespace reboot::os_windows::platform {

namespace {

constexpr const wchar_t* kSchemeKey = L"Software\\Classes\\reboot";
constexpr const wchar_t* kSchemeCommandKey = L"Software\\Classes\\reboot\\shell\\open\\command";
constexpr const wchar_t* kSchemeIconKey = L"Software\\Classes\\reboot\\DefaultIcon";
constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kStartupApprovedKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
constexpr const wchar_t* kRunValue = L"Reboot Launcher";
// In the root folder: a standard user may register tasks there but not create folders.
constexpr std::string_view kTaskPrefix = "Reboot Launcher Engine ";
constexpr std::string_view kSchemeArgs = "--activate-url \"%1\"";
constexpr std::string_view kAutostartArgs = "run --origin=service-manager";
constexpr std::string_view kAgentArgs = "run --origin=on-demand";
// Inside the 4-6 band Task Scheduler maps to a normal priority class; its default is below normal.
constexpr int kTaskPriority = 5;

[[nodiscard]] bool registry_absent(LSTATUS status) noexcept {
    return status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND;
}

[[nodiscard]] Result<std::optional<std::wstring>> read_string(const wchar_t* subkey, const wchar_t* name) {
    DWORD size = 0;
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, subkey, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND,
                                  nullptr, nullptr, &size);
    if (registry_absent(status)) return std::optional<std::wstring>{};
    if (status != ERROR_SUCCESS) return std::unexpected(call_failed("RegGetValueW", static_cast<u32>(status)));
    std::wstring value(size / sizeof(wchar_t) + 1, L'\0');
    size = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    status = RegGetValueW(HKEY_CURRENT_USER, subkey, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr,
                          value.data(), &size);
    if (registry_absent(status)) return std::optional<std::wstring>{};
    if (status != ERROR_SUCCESS) return std::unexpected(call_failed("RegGetValueW", static_cast<u32>(status)));
    value.resize(wcsnlen(value.c_str(), value.size()));
    return std::optional<std::wstring>{std::move(value)};
}

[[nodiscard]] Result<std::optional<std::vector<u8>>> read_binary(const wchar_t* subkey, const wchar_t* name) {
    std::vector<u8> value(64);
    DWORD size = static_cast<DWORD>(value.size());
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, subkey, name, RRF_RT_REG_BINARY, nullptr, value.data(), &size);
    if (registry_absent(status)) return std::optional<std::vector<u8>>{};
    if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA)
        return std::unexpected(call_failed("RegGetValueW", static_cast<u32>(status)));
    value.resize(status == ERROR_MORE_DATA ? value.size() : size);
    return std::optional<std::vector<u8>>{std::move(value)};
}

[[nodiscard]] Result<void> write_string(const wchar_t* subkey, const wchar_t* name, const std::wstring& value) {
    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                                     &key, nullptr);
    if (status != ERROR_SUCCESS) return std::unexpected(call_failed("RegCreateKeyExW", static_cast<u32>(status)));
    status = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) return std::unexpected(call_failed("RegSetValueExW", static_cast<u32>(status)));
    return {};
}

[[nodiscard]] Result<void> delete_value(const wchar_t* subkey, const wchar_t* name) {
    const LSTATUS status = RegDeleteKeyValueW(HKEY_CURRENT_USER, subkey, name);
    if (status != ERROR_SUCCESS && !registry_absent(status))
        return std::unexpected(call_failed("RegDeleteKeyValueW", static_cast<u32>(status)));
    return {};
}

[[nodiscard]] Result<void> delete_tree(const wchar_t* subkey) {
    const LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, subkey);
    if (status != ERROR_SUCCESS && !registry_absent(status))
        return std::unexpected(call_failed("RegDeleteTreeW", static_cast<u32>(status)));
    return {};
}

[[nodiscard]] bool program_exists(const NativePath& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

[[nodiscard]] ports::IntegrationStatus classify(ports::IntegrationKind kind, const std::string& command,
                                                const NativePath& install_root) {
    const std::optional<NativePath> program = command_program(command);
    const ports::IntegrationState state = program ? ownership(*program, program_exists(*program), install_root)
                                                  : ports::IntegrationState::Foreign;
    return ports::IntegrationStatus{kind, state, command};
}

[[nodiscard]] bool removable(ports::IntegrationState state) noexcept {
    return state == ports::IntegrationState::Ours || state == ports::IntegrationState::Stale;
}

// --- Task Scheduler ---------------------------------------------------------------------------

[[nodiscard]] bool task_absent(HRESULT hr) noexcept {
    return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
}

[[nodiscard]] Result<ComPtr<ITaskService>> connect_scheduler() {
    ComPtr<ITaskService> service;
    if (const HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService,
                                            service.put_void());
        FAILED(hr))
        return std::unexpected(hresult_failed("CoCreateInstance", hr));
    VARIANT empty;
    VariantInit(&empty);
    if (const HRESULT hr = service->Connect(empty, empty, empty, empty); FAILED(hr))
        return std::unexpected(hresult_failed("ITaskService::Connect", hr));
    return service;
}

[[nodiscard]] Result<ComPtr<ITaskFolder>> task_folder(ITaskService& service, std::wstring_view path) {
    ComPtr<ITaskFolder> folder;
    const Bstr name(std::wstring(path).c_str());
    if (const HRESULT hr = service.GetFolder(name.get(), folder.put()); FAILED(hr))
        return std::unexpected(hresult_failed("ITaskService::GetFolder", hr));
    return folder;
}

// The exec action's command, or nullopt when the task is missing; "disabled" when switched off.
[[nodiscard]] Result<std::optional<std::string>> task_command(const std::string& task_name) {
    auto service = connect_scheduler();
    if (!service) return std::unexpected(std::move(service.error()));
    auto root = task_folder(*service->get(), L"\\");
    if (!root) return std::unexpected(std::move(root.error()));
    ComPtr<IRegisteredTask> task;
    const Bstr path(widen("\\" + task_name).c_str());
    if (const HRESULT hr = (*root)->GetTask(path.get(), task.put()); FAILED(hr)) {
        if (task_absent(hr)) return std::optional<std::string>{};
        return std::unexpected(hresult_failed("ITaskFolder::GetTask", hr));
    }
    ComPtr<ITaskDefinition> definition;
    ComPtr<IActionCollection> actions;
    ComPtr<IAction> action;
    ComPtr<IExecAction> exec;
    if (const HRESULT hr = task->get_Definition(definition.put()); FAILED(hr))
        return std::unexpected(hresult_failed("IRegisteredTask::get_Definition", hr));
    if (FAILED(definition->get_Actions(actions.put())) || FAILED(actions->get_Item(1, action.put())) ||
        FAILED(action->QueryInterface(IID_IExecAction, exec.put_void())))
        return std::optional<std::string>{std::string()};
    Bstr program;
    Bstr arguments;
    if (const HRESULT hr = exec->get_Path(program.put()); FAILED(hr))
        return std::unexpected(hresult_failed("IExecAction::get_Path", hr));
    (void)exec->get_Arguments(arguments.put());
    const std::string args = arguments.get() != nullptr ? narrow(arguments.get()) : std::string();
    return std::optional<std::string>{entry_command(NativePath(program.get() != nullptr ? program.get() : L""), args)};
}

[[nodiscard]] Result<bool> task_enabled(const std::string& task_name) {
    auto service = connect_scheduler();
    if (!service) return std::unexpected(std::move(service.error()));
    auto root = task_folder(*service->get(), L"\\");
    if (!root) return std::unexpected(std::move(root.error()));
    ComPtr<IRegisteredTask> task;
    const Bstr path(widen("\\" + task_name).c_str());
    if (const HRESULT hr = (*root)->GetTask(path.get(), task.put()); FAILED(hr))
        return std::unexpected(hresult_failed("ITaskFolder::GetTask", hr));
    VARIANT_BOOL enabled = VARIANT_TRUE;
    (void)task->get_Enabled(&enabled);
    return enabled != VARIANT_FALSE;
}

[[nodiscard]] Result<void> register_task(const std::string& user_sid, const NativePath& exe) {
    auto service = connect_scheduler();
    if (!service) return std::unexpected(std::move(service.error()));
    auto root = task_folder(*service->get(), L"\\");
    if (!root) return std::unexpected(std::move(root.error()));

    ComPtr<ITaskDefinition> definition;
    if (const HRESULT hr = (*service)->NewTask(0, definition.put()); FAILED(hr))
        return std::unexpected(hresult_failed("ITaskService::NewTask", hr));

    ComPtr<IPrincipal> principal;
    ComPtr<ITaskSettings> settings;
    ComPtr<IActionCollection> actions;
    ComPtr<IAction> action;
    ComPtr<IExecAction> exec;
    const Bstr sid(widen(user_sid).c_str());
    const Bstr unlimited(L"PT0S");
    const Bstr program(shell_path(exe).c_str());
    const Bstr arguments(widen(kAgentArgs).c_str());
    const Bstr directory(shell_path(exe.parent_path()).c_str());
    HRESULT hr = definition->get_Principal(principal.put());
    if (SUCCEEDED(hr)) hr = principal->put_UserId(sid.get());
    if (SUCCEEDED(hr)) hr = principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
    if (SUCCEEDED(hr)) hr = principal->put_RunLevel(TASK_RUNLEVEL_LUA);
    if (SUCCEEDED(hr)) hr = definition->get_Settings(settings.put());
    if (SUCCEEDED(hr)) hr = settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
    if (SUCCEEDED(hr)) hr = settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
    if (SUCCEEDED(hr)) hr = settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
    if (SUCCEEDED(hr)) hr = settings->put_ExecutionTimeLimit(unlimited.get());
    if (SUCCEEDED(hr)) hr = settings->put_AllowDemandStart(VARIANT_TRUE);
    if (SUCCEEDED(hr)) hr = settings->put_StartWhenAvailable(VARIANT_FALSE);
    if (SUCCEEDED(hr)) hr = settings->put_Priority(kTaskPriority);
    if (SUCCEEDED(hr)) hr = settings->put_Enabled(VARIANT_TRUE);
    if (SUCCEEDED(hr)) hr = definition->get_Actions(actions.put());
    if (SUCCEEDED(hr)) hr = actions->Create(TASK_ACTION_EXEC, action.put());
    if (SUCCEEDED(hr)) hr = action->QueryInterface(IID_IExecAction, exec.put_void());
    if (SUCCEEDED(hr)) hr = exec->put_Path(program.get());
    if (SUCCEEDED(hr)) hr = exec->put_Arguments(arguments.get());
    if (SUCCEEDED(hr)) hr = exec->put_WorkingDirectory(directory.get());
    if (FAILED(hr)) return std::unexpected(hresult_failed("ITaskDefinition", hr));

    const Bstr name(widen(engine_task_name(user_sid)).c_str());
    VARIANT none;
    VariantInit(&none);
    ComPtr<IRegisteredTask> registered;
    if (const HRESULT registered_hr = (*root)->RegisterTaskDefinition(name.get(), definition.get(), TASK_CREATE_OR_UPDATE, none,
                                                                      none, TASK_LOGON_INTERACTIVE_TOKEN, none,
                                                                      registered.put());
        FAILED(registered_hr))
        return std::unexpected(hresult_failed("ITaskFolder::RegisterTaskDefinition", registered_hr));
    return {};
}

[[nodiscard]] Result<void> delete_task(const std::string& task_name) {
    auto service = connect_scheduler();
    if (!service) return std::unexpected(std::move(service.error()));
    auto root = task_folder(*service->get(), L"\\");
    if (!root) return std::unexpected(std::move(root.error()));
    const Bstr path(widen("\\" + task_name).c_str());
    if (const HRESULT hr = (*root)->DeleteTask(path.get(), 0); FAILED(hr) && !task_absent(hr))
        return std::unexpected(hresult_failed("ITaskFolder::DeleteTask", hr));
    return {};
}

template <class T, class Body>
[[nodiscard]] Result<T> in_mta(std::string_view where, Body body) {
    return in_apartment<T>(COINIT_MULTITHREADED, where, std::move(body));
}

}  // namespace

std::string engine_task_name(std::string_view user_sid) { return std::string(kTaskPrefix) + std::string(user_sid); }

WindowsIntegrationRegistrar::WindowsIntegrationRegistrar(NativePath install_root, std::string user_sid)
    : install_root_(std::move(install_root)), user_sid_(std::move(user_sid)) {}

Result<ports::IntegrationStatus> WindowsIntegrationRegistrar::status(ports::IntegrationKind kind) {
    const ports::IntegrationStatus absent{kind, ports::IntegrationState::Absent, {}};
    switch (kind) {
        case ports::IntegrationKind::UrlScheme: {
            auto command = read_string(kSchemeCommandKey, nullptr);
            if (!command) return std::unexpected(std::move(command.error()));
            if (!*command) return absent;
            return classify(kind, narrow(**command), install_root_);
        }
        case ports::IntegrationKind::Autostart: {
            auto command = read_string(kRunKey, kRunValue);
            if (!command) return std::unexpected(std::move(command.error()));
            if (!*command) return absent;
            ports::IntegrationStatus found = classify(kind, narrow(**command), install_root_);
            if (found.state != ports::IntegrationState::Ours) return found;
            auto approved = read_binary(kStartupApprovedKey, kRunValue);
            if (!approved) return std::unexpected(std::move(approved.error()));
            if (*approved && startup_disabled(**approved)) found.detail = "disabled";
            return found;
        }
        case ports::IntegrationKind::EngineAgent: {
            const std::string name = engine_task_name(user_sid_);
            auto command = in_mta<std::optional<std::string>>("WindowsIntegrationRegistrar::status",
                                                              [&] { return task_command(name); });
            if (!command) return std::unexpected(std::move(command.error()));
            if (!*command) return absent;
            ports::IntegrationStatus found = classify(kind, **command, install_root_);
            if (found.state != ports::IntegrationState::Ours) return found;
            auto enabled = in_mta<bool>("WindowsIntegrationRegistrar::status", [&] { return task_enabled(name); });
            if (enabled && !*enabled) found.detail = "disabled";
            return found;
        }
        case ports::IntegrationKind::DesktopEntry: return absent;
    }
    return absent;
}

Result<void> WindowsIntegrationRegistrar::apply(ports::IntegrationKind kind, const NativePath& exe) {
    switch (kind) {
        case ports::IntegrationKind::UrlScheme: {
            if (auto cleared = delete_tree(kSchemeKey); !cleared) return cleared;
            if (auto named = write_string(kSchemeKey, nullptr, L"URL:Reboot Launcher"); !named) return named;
            if (auto marked = write_string(kSchemeKey, L"URL Protocol", L""); !marked) return marked;
            if (auto icon = write_string(kSchemeIconKey, nullptr, L"\"" + shell_path(exe) + L"\",0"); !icon) return icon;
            if (auto command = write_string(kSchemeCommandKey, nullptr, widen(entry_command(NativePath(shell_path(exe)), kSchemeArgs)));
                !command)
                return command;
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
            return {};
        }
        case ports::IntegrationKind::Autostart:
            return write_string(kRunKey, kRunValue, widen(entry_command(NativePath(shell_path(exe)), kAutostartArgs)));
        case ports::IntegrationKind::EngineAgent:
            return in_mta<void>("WindowsIntegrationRegistrar::apply", [&] { return register_task(user_sid_, exe); });
        case ports::IntegrationKind::DesktopEntry: break;
    }
    return make_diag(ErrorDomain::Platform, kNotSupported).kind(ErrorKind::Unsupported).fail();
}

Result<void> WindowsIntegrationRegistrar::remove(ports::IntegrationKind kind) {
    auto found = status(kind);
    if (!found) return std::unexpected(std::move(found.error()));
    if (!removable(found->state)) return {};
    switch (kind) {
        case ports::IntegrationKind::UrlScheme: {
            if (auto removed = delete_tree(kSchemeKey); !removed) return removed;
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
            return {};
        }
        case ports::IntegrationKind::Autostart: {
            if (auto removed = delete_value(kRunKey, kRunValue); !removed) return removed;
            return delete_value(kStartupApprovedKey, kRunValue);
        }
        case ports::IntegrationKind::EngineAgent: {
            const std::string name = engine_task_name(user_sid_);
            return in_mta<void>("WindowsIntegrationRegistrar::remove", [&] { return delete_task(name); });
        }
        case ports::IntegrationKind::DesktopEntry: break;
    }
    return {};
}

}  // namespace reboot::os_windows::platform
