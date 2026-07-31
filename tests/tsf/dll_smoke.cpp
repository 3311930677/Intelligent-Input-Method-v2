#include <Windows.h>
#include <msctf.h>

#include <iostream>

namespace {
constexpr CLSID kTextServiceClsid{
    0x6d31c9b1, 0x8978, 0x4f49, {0x89, 0xb4, 0x66, 0xeb, 0x1e, 0x74, 0x15, 0x91}};
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const HMODULE module = LoadLibraryW(argv[1]);
    if (module == nullptr) {
        std::cerr << "LoadLibraryW failed: " << GetLastError() << '\n';
        return 3;
    }
    using GetClassObject = HRESULT(__stdcall*)(REFCLSID, REFIID, void**);
    const auto get_class_object = reinterpret_cast<GetClassObject>(
        GetProcAddress(module, "DllGetClassObject"));
    if (get_class_object == nullptr) {
        FreeLibrary(module);
        return 4;
    }
    IClassFactory* factory = nullptr;
    HRESULT result = get_class_object(kTextServiceClsid, IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        FreeLibrary(module);
        return 5;
    }
    ITfTextInputProcessorEx* service = nullptr;
    result = factory->CreateInstance(nullptr, IID_PPV_ARGS(&service));
    factory->Release();
    if (FAILED(result)) {
        FreeLibrary(module);
        return 6;
    }
    service->Release();
    FreeLibrary(module);
    return 0;
}

