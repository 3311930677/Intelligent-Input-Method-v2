#include <Windows.h>
#include <msctf.h>

#include <iostream>

namespace {
constexpr CLSID kTextServiceClsid{
    0x6d31c9b1, 0x8978, 0x4f49, {0x89, 0xb4, 0x66, 0xeb, 0x1e, 0x74, 0x15, 0x91}};
constexpr GUID kLanguageProfileGuid{
    0x5d9f39c3, 0xbdb4, 0x453c, {0xa7, 0xba, 0xb9, 0xef, 0x82, 0x48, 0x76, 0x29}};
constexpr LANGID kSimplifiedChinese = 0x0804;
}

int main() {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(result)) return 2;

    ITfInputProcessorProfiles* profiles = nullptr;
    result = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles));
    if (FAILED(result)) {
        CoUninitialize();
        return 3;
    }

    IEnumTfLanguageProfiles* enumeration = nullptr;
    result = profiles->EnumLanguageProfiles(kSimplifiedChinese, &enumeration);
    bool found = false;
    if (SUCCEEDED(result)) {
        TF_LANGUAGEPROFILE profile{};
        ULONG fetched = 0;
        while (enumeration->Next(1, &profile, &fetched) == S_OK) {
            if (profile.clsid == kTextServiceClsid &&
                profile.guidProfile == kLanguageProfileGuid) {
                found = true;
                std::cout << "OwO TSF profile found; active="
                          << (profile.fActive ? "true" : "false") << '\n';
                break;
            }
        }
        enumeration->Release();
    }
    profiles->Release();
    CoUninitialize();
    return found ? 0 : 4;
}

