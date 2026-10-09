#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "session_mapping.hpp"

#include <cstring>
#include <string>
#include <type_traits>
#include <variant>

#include "messages.hpp"
#include "win_error.hpp"

namespace rb::os_windows::platform {

namespace {

namespace wh = contracts::winhost;
using win32session::InjectStep;
using win32session::SpawnStep;

[[nodiscard]] ports::SessionRole role_of(wh::ProcessRole role) noexcept {
    return role == wh::ProcessRole::Game ? ports::SessionRole::Game : ports::SessionRole::Companion;
}

[[nodiscard]] std::string_view spawn_call(SpawnStep step) noexcept {
    switch (step) {
        case SpawnStep::CreateJob: return "CreateJobObjectW";
        case SpawnStep::ConfigureJob: return "SetInformationJobObject";
        case SpawnStep::CreatePipe: return "CreatePipe";
        case SpawnStep::AttributeList: return "UpdateProcThreadAttribute";
        case SpawnStep::CreateGame:
        case SpawnStep::CreateCompanion: return "CreateProcessW";
        case SpawnStep::Inject: return "LoadLibraryW";
        case SpawnStep::Resume: return "ResumeThread";
        case SpawnStep::Watch: return "CreateThread";
    }
    return "launch_session";
}

[[nodiscard]] std::string_view inject_call(InjectStep step) noexcept {
    switch (step) {
        case InjectStep::OpenFile: return "CreateFileW";
        case InjectStep::Integrity: return "BCryptHashData";
        case InjectStep::Allocate: return "VirtualAllocEx";
        case InjectStep::Write: return "WriteProcessMemory";
        case InjectStep::ResolveLoader: return "GetProcAddress";
        case InjectStep::QueueApc: return "QueueUserAPC";
        case InjectStep::CreateThread: return "CreateRemoteThread";
        case InjectStep::WaitThread: return "WaitForSingleObject";
        case InjectStep::RemoteLoad: return "LoadLibraryW";
        case InjectStep::Confirm: return "EnumProcessModulesEx";
    }
    return "inject";
}

[[nodiscard]] u32 win32_code(const SystemError& error) noexcept { return static_cast<u32>(error.code); }

}  // namespace

contracts::winhost::Bytes utf16_bytes(std::wstring_view text) {
    static_assert(sizeof(wchar_t) == 2, "Windows strings are UTF-16");
    contracts::winhost::Bytes bytes(text.size() * 2);
    std::memcpy(bytes.data(), text.data(), bytes.size());
    return bytes;
}

NativePath path_of(const contracts::winhost::Bytes& utf16le) {
    std::wstring text(utf16le.size() / 2, L'\0');
    std::memcpy(text.data(), utf16le.data(), text.size() * 2);
    while (!text.empty() && text.back() == L'\0') text.pop_back();
    return NativePath(std::move(text));
}

ports::SessionHostEvent to_port_event(const win32session::SessionEvent& event) {
    return std::visit(
        [](const auto& value) -> ports::SessionHostEvent {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, wh::Spawned>) {
                return ports::Spawned{role_of(value.role), value.pid};
            } else if constexpr (std::is_same_v<T, wh::Injected>) {
                ports::Injected injected{path_of(value.path_utf16), value.ok, std::nullopt};
                if (value.error) injected.error = SystemError{SystemError::Origin::Host, *value.error};
                return injected;
            } else if constexpr (std::is_same_v<T, wh::Output>) {
                return ports::Output{role_of(value.role), value.stream, value.bytes};
            } else {
                ports::Exited exited{role_of(value.role), std::nullopt};
                if (value.code) exited.code = static_cast<int>(*value.code);
                return exited;
            }
        },
        event);
}

Diagnostic spawn_diagnostic(const win32session::SpawnError& error, const NativePath& exe) {
    if (error.step == SpawnStep::CreateGame) return call_failed(spawn_call(error.step), win32_code(error.error), exe);
    return call_failed(spawn_call(error.step), win32_code(error.error));
}

Diagnostic inject_diagnostic(const win32session::InjectError& error, const NativePath& dll) {
    if (error.step == InjectStep::OpenFile)
        return make_diag(ErrorDomain::Platform, kFileVanished).arg("path", dll).os(error.error).kind(ErrorKind::NotFound);
    if (error.step == InjectStep::Integrity && win32_code(error.error) == ERROR_INVALID_IMAGE_HASH)
        return make_diag(ErrorDomain::Platform, kPayloadHashMismatch).arg("path", dll).os(error.error);
    return call_failed(inject_call(error.step), win32_code(error.error), dll);
}

}  // namespace rb::os_windows::platform
