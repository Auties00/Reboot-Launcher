#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/wmi_security_product_probe.hpp"

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/operation.hpp"
#include "unique_handle.hpp"
#include "wide.hpp"
#include "win_error.hpp"

namespace reboot::os_windows::platform {

namespace {

// nullopt when the OS has no Security Center namespace, as on Windows Server.
using Names = std::optional<std::vector<std::string>>;

struct Answer {
    std::mutex mutex;
    std::condition_variable ready;
    std::optional<Result<Names>> result;
};

[[nodiscard]] Result<Names> query_products() {
    ComPtr<IWbemLocator> locator;
    if (const HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, locator.put_void());
        FAILED(hr))
        return std::unexpected(hresult_failed("CoCreateInstance", hr));
    ComPtr<IWbemServices> services;
    const Bstr space(L"ROOT\\SecurityCenter2");
    if (const HRESULT hr = locator->ConnectServer(space.get(), nullptr, nullptr, nullptr, 0, nullptr, nullptr, services.put());
        FAILED(hr)) {
        if (hr == static_cast<HRESULT>(WBEM_E_INVALID_NAMESPACE)) return Names{};
        return std::unexpected(hresult_failed("IWbemLocator::ConnectServer", hr));
    }
    if (const HRESULT hr = CoSetProxyBlanket(services.get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                                             RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        FAILED(hr))
        return std::unexpected(hresult_failed("CoSetProxyBlanket", hr));
    ComPtr<IEnumWbemClassObject> rows;
    const Bstr language(L"WQL");
    const Bstr query(L"SELECT displayName FROM AntiVirusProduct");
    if (const HRESULT hr = services->ExecQuery(language.get(), query.get(), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                               nullptr, rows.put());
        FAILED(hr))
        return std::unexpected(hresult_failed("IWbemServices::ExecQuery", hr));
    std::vector<std::string> names;
    for (;;) {
        ComPtr<IWbemClassObject> row;
        ULONG returned = 0;
        const HRESULT hr = rows->Next(static_cast<long>(WBEM_INFINITE), 1, row.put(), &returned);
        if (FAILED(hr)) return std::unexpected(hresult_failed("IEnumWbemClassObject::Next", hr));
        if (returned == 0) break;
        VARIANT value;
        VariantInit(&value);
        if (SUCCEEDED(row->Get(L"displayName", 0, &value, nullptr, nullptr)) && value.vt == VT_BSTR && value.bstrVal != nullptr) {
            std::string name = narrow(value.bstrVal);
            if (!name.empty() && std::ranges::find(names, name) == names.end()) names.push_back(std::move(name));
        }
        VariantClear(&value);
    }
    return Names{std::move(names)};
}

[[nodiscard]] ports::SmartAppControl smart_app_control() noexcept {
    DWORD state = 0;
    DWORD size = sizeof state;
    const LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\CI\\Policy",
                                        L"VerifiedAndReputablePolicyState", RRF_RT_REG_DWORD, nullptr, &state, &size);
    // Builds before Smart App Control have no such value.
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) return ports::SmartAppControl::Off;
    if (status != ERROR_SUCCESS) return ports::SmartAppControl::Unknown;
    switch (state) {
        case 0: return ports::SmartAppControl::Off;
        case 1: return ports::SmartAppControl::On;
        case 2: return ports::SmartAppControl::Evaluation;
        default: return ports::SmartAppControl::Unknown;
    }
}

}  // namespace

Result<std::optional<ports::SecurityProducts>> WmiSecurityProductProbe::probe() {
    const std::chrono::milliseconds deadline = default_deadline(OpKind::Wmi);
    auto answer = std::make_shared<Answer>();
    try {
        std::thread([answer] {
            Result<Names> result = std::unexpected(internal_bug("WmiSecurityProductProbe"));
            const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(init)) {
                result = std::unexpected(hresult_failed("CoInitializeEx", init));
            } else {
                try {
                    result = query_products();
                } catch (...) {
                    result = std::unexpected(internal_bug("WmiSecurityProductProbe"));
                }
                CoUninitialize();
            }
            std::scoped_lock lock(answer->mutex);
            answer->result = std::move(result);
            answer->ready.notify_all();
        }).detach();
    } catch (...) {
        return std::unexpected(internal_bug("WmiSecurityProductProbe"));
    }

    std::unique_lock lock(answer->mutex);
    if (!answer->ready.wait_for(lock, deadline, [&] { return answer->result.has_value(); }))
        return make_diag(ErrorDomain::Platform, kWmiTimeout).arg("deadline", deadline).retryable().fail();
    Result<Names> names = std::move(*answer->result);
    lock.unlock();
    if (!names) return std::unexpected(std::move(names.error()));
    if (!*names) return std::optional<ports::SecurityProducts>{};
    return std::optional<ports::SecurityProducts>{ports::SecurityProducts{std::move(**names), smart_app_control()}};
}

}  // namespace reboot::os_windows::platform
