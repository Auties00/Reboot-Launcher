#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "remote_injector.hpp"

#include <array>
#include <cstring>
#include <vector>

#include "wide.hpp"

namespace reboot::os_windows::win32session {
namespace {

using contracts::winhost::InjectSpec;

std::unexpected<InjectError> fail(InjectStep step, DWORD code) {
    return std::unexpected(InjectError{step, SystemError{SystemError::Origin::Host, static_cast<i64>(code)}});
}

std::wstring base_name_of(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// Hashes the open file with BCrypt so the package needs no OpenSSL and no foundation library.
std::expected<std::array<u8, 32>, DWORD> hash_file(HANDLE file) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return std::unexpected<DWORD>(ERROR_INVALID_FUNCTION);
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD fault = 0;
    std::array<u8, 32> digest{};
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) != 0) {
        fault = ERROR_INVALID_FUNCTION;
    } else {
        std::array<u8, 64 * 1024> buffer{};
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
                fault = GetLastError();
                break;
            }
            if (read == 0) break;
            if (BCryptHashData(hash, buffer.data(), read, 0) != 0) {
                fault = ERROR_INVALID_FUNCTION;
                break;
            }
        }
        if (fault == 0 && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) != 0)
            fault = ERROR_INVALID_FUNCTION;
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    if (fault != 0) return std::unexpected<DWORD>(fault);
    return digest;
}

// LoadLibraryW's address in this process; kernel32 is mapped at the same base in the target. The
// cast goes through a generic function pointer so it stays a function-to-function conversion.
void (*load_library_w())() {
    return reinterpret_cast<void (*)()>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
}

bool digests_equal(const std::array<u8, 32>& a, const std::array<u8, 32>& b) noexcept {
    u8 diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff |= static_cast<u8>(a[i] ^ b[i]);
    return diff == 0;
}

}  // namespace

bool module_loaded(HANDLE process, const std::wstring& base_name) {
    std::vector<HMODULE> modules(256);
    DWORD needed = 0;
    for (;;) {
        if (!EnumProcessModulesEx(process, modules.data(), static_cast<DWORD>(modules.size() * sizeof(HMODULE)),
                                  &needed, LIST_MODULES_ALL))
            return false;
        const std::size_t count = needed / sizeof(HMODULE);
        if (count <= modules.size()) {
            modules.resize(count);
            break;
        }
        modules.resize(count);
    }
    for (HMODULE module : modules) {
        std::array<wchar_t, MAX_PATH> name{};
        if (GetModuleBaseNameW(process, module, name.data(), static_cast<DWORD>(name.size())) == 0) continue;
        if (_wcsicmp(name.data(), base_name.c_str()) == 0) return true;
    }
    return false;
}

std::expected<void, InjectError> RemoteInjector::prepare(const InjectSpec& spec, InjectedDll& out) {
    const std::wstring path = to_wide(spec.path_utf16);

    // deny-write and deny-delete: FILE_SHARE_READ only, so the file cannot be swapped after the hash.
    UniqueHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) return fail(InjectStep::OpenFile, GetLastError());

    const auto digest = hash_file(file.get());
    if (!digest) return fail(InjectStep::Integrity, digest.error());
    if (!digests_equal(*digest, spec.sha256)) return fail(InjectStep::Integrity, ERROR_INVALID_IMAGE_HASH);

    // Full byte length of the UTF-16 path plus its terminator; VirtualAllocEx zero-fills, so the
    // written run stays NUL-terminated even if the copy stops at the path bytes.
    const std::size_t bytes = (path.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process_, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote == nullptr) return fail(InjectStep::Allocate, GetLastError());

    if (!WriteProcessMemory(process_, remote, path.c_str(), bytes, nullptr)) {
        const DWORD code = GetLastError();
        VirtualFreeEx(process_, remote, 0, MEM_RELEASE);
        return fail(InjectStep::Write, code);
    }

    out.file = std::move(file);
    out.path_utf16 = spec.path_utf16;
    out.base_name = base_name_of(path);
    out.remote_path = remote;
    out.remote_size = bytes;
    return {};
}

std::expected<InjectedDll, InjectError> RemoteInjector::queue_early(const InjectSpec& spec, HANDLE thread) {
    InjectedDll result;
    if (auto prepared = prepare(spec, result); !prepared) return std::unexpected(prepared.error());

    auto loader = reinterpret_cast<PAPCFUNC>(load_library_w());
    if (loader == nullptr) {
        const DWORD code = GetLastError();
        VirtualFreeEx(process_, result.remote_path, 0, MEM_RELEASE);
        return fail(InjectStep::ResolveLoader, code);
    }
    if (QueueUserAPC(loader, thread, reinterpret_cast<ULONG_PTR>(result.remote_path)) == 0) {
        const DWORD code = GetLastError();
        VirtualFreeEx(process_, result.remote_path, 0, MEM_RELEASE);
        return fail(InjectStep::QueueApc, code);
    }
    return result;  // remote_path stays live until the APC runs and the load is confirmed
}

std::expected<void, InjectError> RemoteInjector::load_now(const InjectSpec& spec, std::chrono::milliseconds timeout,
                                                          InjectedDll& out) {
    if (auto prepared = prepare(spec, out); !prepared) return std::unexpected(prepared.error());

    auto loader = reinterpret_cast<LPTHREAD_START_ROUTINE>(load_library_w());
    if (loader == nullptr) {
        const DWORD code = GetLastError();
        VirtualFreeEx(process_, out.remote_path, 0, MEM_RELEASE);
        out.remote_path = nullptr;
        out.remote_size = 0;
        return fail(InjectStep::ResolveLoader, code);
    }

    // CreateRemoteThread returns NULL on failure, not -1.
    UniqueHandle remote_thread(CreateRemoteThread(process_, nullptr, 0, loader, out.remote_path, 0, nullptr));
    if (!remote_thread) {
        const DWORD code = GetLastError();
        VirtualFreeEx(process_, out.remote_path, 0, MEM_RELEASE);
        out.remote_path = nullptr;
        out.remote_size = 0;
        return fail(InjectStep::CreateThread, code);
    }

    const DWORD waited = WaitForSingleObject(remote_thread.get(), wait_millis(timeout));
    if (waited != WAIT_OBJECT_0) {
        // The thread may still be reading the path, so the buffer stays until the process ends.
        return fail(InjectStep::WaitThread, waited == WAIT_FAILED ? GetLastError() : WAIT_TIMEOUT);
    }

    DWORD exit_code = 0;  // low 32 bits of the remote HMODULE
    const bool got_exit = GetExitCodeThread(remote_thread.get(), &exit_code) != 0;
    const DWORD exit_error = got_exit ? 0 : GetLastError();
    VirtualFreeEx(process_, out.remote_path, 0, MEM_RELEASE);
    out.remote_path = nullptr;
    out.remote_size = 0;
    if (!got_exit) return fail(InjectStep::WaitThread, exit_error);
    // A 64-bit module base can have zero low bits, so a zero exit code is checked against the module list.
    if (exit_code == 0 && !module_loaded(process_, out.base_name))
        return fail(InjectStep::RemoteLoad, ERROR_MOD_NOT_FOUND);
    return {};
}

DWORD wait_millis(std::chrono::milliseconds timeout) noexcept {
    if (timeout.count() <= 0) return 0;
    if (timeout.count() >= static_cast<std::chrono::milliseconds::rep>(INFINITE)) return INFINITE - 1;
    return static_cast<DWORD>(timeout.count());
}

}  // namespace reboot::os_windows::win32session
