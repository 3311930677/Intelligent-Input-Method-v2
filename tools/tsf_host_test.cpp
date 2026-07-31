#include <Windows.h>
#include <msctf.h>

#include <string>
#include <iterator>
#include <string_view>

namespace {
constexpr CLSID kTextServiceClsid{
    0x6d31c9b1, 0x8978, 0x4f49, {0x89, 0xb4, 0x66, 0xeb, 0x1e, 0x74, 0x15, 0x91}};
constexpr GUID kLanguageProfileGuid{
    0x5d9f39c3, 0xbdb4, 0x453c, {0xa7, 0xba, 0xb9, 0xef, 0x82, 0x48, 0x76, 0x29}};
constexpr LANGID kSimplifiedChinese = 0x0804;
constexpr wchar_t kWindowClass[] = L"OwO.P1.TsfHostTest";
HWND edit_control = nullptr;
int test_phase = 0;

void send_key(const WORD virtual_key) {
    PostMessageW(edit_control, WM_KEYDOWN, virtual_key, 1);
    PostMessageW(edit_control, WM_KEYUP, virtual_key,
                 static_cast<LPARAM>(1U | (1U << 30U) | (1U << 31U)));
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE:
            edit_control = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                20, 20, 360, 32, window, nullptr, GetModuleHandleW(nullptr), nullptr);
            SetTimer(window, 1, 400, nullptr);
            SetFocus(edit_control);
            return 0;
        case WM_TIMER:
            if (test_phase == 0) {
                SetForegroundWindow(window);
                SetFocus(edit_control);
                send_key('N');
                send_key('I');
                ++test_phase;
            } else if (test_phase == 1) {
                send_key(VK_SPACE);
                ++test_phase;
            } else {
                wchar_t text[64]{};
                GetWindowTextW(edit_control, text, static_cast<int>(std::size(text)));
                KillTimer(window, 1);
                if (GetModuleHandleW(L"OwO.TSF.dll") == nullptr) {
                    PostQuitMessage(20);
                } else {
                    PostQuitMessage(std::wstring_view(text) == L"固定候选" ? 0 : 10);
                }
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(11);
            return 0;
        default:
            return DefWindowProcW(window, message, wparam, lparam);
    }
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, wchar_t*, int) {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(result)) return 2;

    ITfThreadMgr* thread_manager = nullptr;
    result = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&thread_manager));
    TfClientId client_id = TF_CLIENTID_NULL;
    if (SUCCEEDED(result)) result = thread_manager->Activate(&client_id);

    ITfInputProcessorProfileMgr* profile_manager = nullptr;
    ITfKeystrokeMgr* keystroke_manager = nullptr;
    if (SUCCEEDED(result)) {
        result = thread_manager->QueryInterface(IID_PPV_ARGS(&keystroke_manager));
    }
    if (SUCCEEDED(result)) {
        result = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profile_manager));
    }
    if (SUCCEEDED(result)) {
        result = profile_manager->ActivateProfile(
            TF_PROFILETYPE_INPUTPROCESSOR, kSimplifiedChinese, kTextServiceClsid,
            kLanguageProfileGuid, nullptr,
            TF_IPPMF_FORPROCESS | TF_IPPMF_ENABLEPROFILE |
                TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
    }

    int exit_code = 3;
    if (SUCCEEDED(result)) {
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = instance;
        window_class.lpszClassName = kWindowClass;
        window_class.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (RegisterClassW(&window_class) != 0) {
            const HWND window = CreateWindowExW(
                0, kWindowClass, L"OwO TSF isolated host test", WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT, CW_USEDEFAULT, 420, 110, nullptr, nullptr, instance, nullptr);
            if (window != nullptr) {
                ShowWindow(window, SW_SHOW);
                UpdateWindow(window);
                MSG message{};
                BOOL get_message = FALSE;
                while ((get_message = GetMessageW(&message, nullptr, 0, 0)) > 0) {
                    BOOL eaten = FALSE;
                    if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) {
                        if (SUCCEEDED(keystroke_manager->TestKeyDown(
                                message.wParam, message.lParam, &eaten)) && eaten) {
                            keystroke_manager->KeyDown(message.wParam, message.lParam, &eaten);
                            continue;
                        }
                    } else if (message.message == WM_KEYUP || message.message == WM_SYSKEYUP) {
                        if (SUCCEEDED(keystroke_manager->TestKeyUp(
                                message.wParam, message.lParam, &eaten)) && eaten) {
                            keystroke_manager->KeyUp(message.wParam, message.lParam, &eaten);
                            continue;
                        }
                    }
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                exit_code = get_message == -1 ? 4 : static_cast<int>(message.wParam);
            }
        }
    }

    if (profile_manager != nullptr) profile_manager->Release();
    if (keystroke_manager != nullptr) keystroke_manager->Release();
    if (thread_manager != nullptr) {
        thread_manager->Deactivate();
        thread_manager->Release();
    }
    CoUninitialize();
    return exit_code;
}
