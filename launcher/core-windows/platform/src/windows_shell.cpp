#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/windows_shell.hpp"

#include <atomic>
#include <string>

#include "com_thread.hpp"
#include "https_url.hpp"
#include "messages.hpp"
#include "unique_handle.hpp"
#include "wide.hpp"
#include "win_error.hpp"

namespace rb::os_windows::platform {

namespace {

[[nodiscard]] Result<void> shell_open(const std::wstring& target, const NativePath& shown) {
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof info;
    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"open";
    info.lpFile = target.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info) == 0) return std::unexpected(call_failed("ShellExecuteExW", GetLastError(), shown));
    return {};
}

#if defined(__GNUC__)
// COM interfaces have no virtual destructor; Release, not delete, ends their objects.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif

// Vetoes every delete the shell would do permanently, so a volume without a Recycle Bin fails the
// operation instead of losing the file.
class RecycleOnlySink final : public IFileOperationProgressSink {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (object == nullptr) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IFileOperationProgressSink)) {
            *object = static_cast<IFileOperationProgressSink*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    // Lives on the caller's stack and outlives the operation, so the count only tracks.
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { return --references_; }

    HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD flags, IShellItem*) override {
        if ((flags & TSF_DELETE_RECYCLE_IF_POSSIBLE) == 0) {
            refused_ = true;
            return E_ABORT;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE StartOperations() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem*, HRESULT, IShellItem*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UpdateProgress(UINT, UINT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResetTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PauseTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResumeTimer() override { return S_OK; }

    [[nodiscard]] bool refused() const noexcept { return refused_; }

private:
    std::atomic<ULONG> references_{1};
    bool refused_ = false;
};

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

[[nodiscard]] Diagnostic trash_unavailable(const NativePath& path) {
    return make_diag(ErrorDomain::Platform, kTrashUnavailable).arg("path", path).kind(ErrorKind::Unsupported);
}

[[nodiscard]] Result<void> recycle(const NativePath& path) {
    const std::wstring target = shell_path(path);
    ComPtr<IShellItem> item;
    if (const HRESULT hr = SHCreateItemFromParsingName(target.c_str(), nullptr, IID_IShellItem, item.put_void()); FAILED(hr))
        return std::unexpected(hresult_failed("SHCreateItemFromParsingName", hr, path));
    ComPtr<IFileOperation> operation;
    if (const HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_IFileOperation, operation.put_void());
        FAILED(hr))
        return std::unexpected(hresult_failed("CoCreateInstance", hr, path));
    if (const HRESULT hr = operation->SetOperationFlags(FOF_NO_UI | FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE | FOFX_EARLYFAILURE);
        FAILED(hr))
        return std::unexpected(hresult_failed("IFileOperation::SetOperationFlags", hr, path));
    RecycleOnlySink sink;
    DWORD cookie = 0;
    if (const HRESULT hr = operation->Advise(&sink, &cookie); FAILED(hr))
        return std::unexpected(hresult_failed("IFileOperation::Advise", hr, path));
    HRESULT hr = operation->DeleteItem(item.get(), nullptr);
    if (SUCCEEDED(hr)) hr = operation->PerformOperations();
    BOOL aborted = FALSE;
    const bool any_aborted = SUCCEEDED(operation->GetAnyOperationsAborted(&aborted)) && aborted != FALSE;
    operation->Unadvise(cookie);
    if (sink.refused() || hr == E_ABORT || hr == HRESULT_FROM_WIN32(ERROR_CANCELLED) || any_aborted)
        return std::unexpected(trash_unavailable(path));
    if (FAILED(hr)) return std::unexpected(hresult_failed("IFileOperation::PerformOperations", hr, path));
    return {};
}

}  // namespace

Result<void> WindowsShell::open_url(std::string_view https_url) {
    if (!is_https_url(https_url))
        return make_diag(ErrorDomain::Platform, kUrlNotHttps).kind(ErrorKind::InvalidInput).fail();
    const std::wstring target = widen(https_url);
    return in_apartment<void>(COINIT_APARTMENTTHREADED, "WindowsShell::open_url",
                              [&]() -> Result<void> { return shell_open(target, NativePath()); });
}

Result<void> WindowsShell::open_path(const NativePath& path) {
    const std::wstring target = shell_path(path);
    return in_apartment<void>(COINIT_APARTMENTTHREADED, "WindowsShell::open_path",
                              [&]() -> Result<void> { return shell_open(target, path); });
}

Result<void> WindowsShell::reveal(const NativePath& path) {
    const std::wstring target = shell_path(path);
    return in_apartment<void>(COINIT_APARTMENTTHREADED, "WindowsShell::reveal", [&]() -> Result<void> {
        PIDLIST_ABSOLUTE item = nullptr;
        if (const HRESULT hr = SHParseDisplayName(target.c_str(), nullptr, &item, 0, nullptr); FAILED(hr))
            return std::unexpected(hresult_failed("SHParseDisplayName", hr, path));
        const HRESULT hr = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
        CoTaskMemFree(item);
        if (FAILED(hr)) return std::unexpected(hresult_failed("SHOpenFolderAndSelectItems", hr, path));
        return {};
    });
}

Result<void> WindowsShell::trash(const NativePath& path) {
    return in_apartment<void>(COINIT_APARTMENTTHREADED, "WindowsShell::trash", [&]() { return recycle(path); });
}

}  // namespace rb::os_windows::platform
