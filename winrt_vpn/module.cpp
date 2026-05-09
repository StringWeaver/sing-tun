#include "pch.h"
#include <winrt/base.h>

bool __stdcall winrt_can_unload_now() noexcept;
void* __stdcall winrt_get_activation_factory(std::wstring_view const& name);

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(nullptr);
    }
    return TRUE;
}

extern "C" HRESULT __stdcall DllCanUnloadNow()
{
    return winrt_can_unload_now() ? S_OK : S_FALSE;
}

extern "C" HRESULT __stdcall DllGetActivationFactory(HSTRING classId, void** factory)
{
    try
    {
        *factory = winrt_get_activation_factory(winrt::Windows::Foundation::WindowsGetStringRawBuffer(classId, nullptr));
        if (*factory)
        {
            return S_OK;
        }
        return winrt::hresult_class_not_available().to_abi();
    }
    catch (...)
    {
        return winrt::to_hresult();
    }
}
