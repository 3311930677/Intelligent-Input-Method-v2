#include "text_service.h"

#include "owo/ipc/named_pipe.h"
#include "owo/protocol/messages.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <new>
#include <utility>

namespace owo::tsf {
namespace {
LONG object_count = 0;
LONG lock_count = 0;
constexpr wchar_t kMessageClass[] = L"OwO.P1.MessageWindow";
constexpr wchar_t kCandidateClass[] = L"OwO.P1.CandidateWindow";
constexpr UINT kCandidateReady = WM_APP + 1;

std::string utf8_from_wide(const std::wstring_view input) {
    if (input.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
                                         static_cast<int>(input.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
                            static_cast<int>(input.size()), result.data(), size,
                            nullptr, nullptr) != size) {
        return {};
    }
    return result;
}

std::wstring wide_from_utf8(const std::string_view input) {
    if (input.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                                         static_cast<int>(input.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                            static_cast<int>(input.size()), result.data(), size) != size) {
        return {};
    }
    return result;
}

class CommitEditSession final : public ITfEditSession {
public:
    CommitEditSession(ITfContext* context, std::wstring text)
        : context_(context), text_(std::move(text)) {
        context_->AddRef();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_ITfEditSession) {
            *object = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&references_));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = InterlockedDecrement(&references_);
        if (remaining == 0) delete this;
        return static_cast<ULONG>(remaining);
    }
    HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie) override {
        TF_SELECTION selection{};
        ULONG fetched = 0;
        HRESULT result = context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1,
                                                &selection, &fetched);
        if (FAILED(result) || fetched != 1 || selection.range == nullptr) return result;
        result = selection.range->SetText(cookie, 0, text_.data(),
                                          static_cast<LONG>(text_.size()));
        if (SUCCEEDED(result)) {
            selection.range->Collapse(cookie, TF_ANCHOR_END);
            result = context_->SetSelection(cookie, 1, &selection);
        }
        selection.range->Release();
        return result;
    }

private:
    ~CommitEditSession() { context_->Release(); }
    LONG references_{1};
    ITfContext* context_;
    std::wstring text_;
};
}  // namespace

TextService::TextService() noexcept {
    InterlockedIncrement(&object_count);
}

TextService::~TextService() {
    Deactivate();
    InterlockedDecrement(&object_count);
}

HRESULT TextService::QueryInterface(REFIID iid, void** object) {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
    if (iid == IID_IUnknown || iid == IID_ITfTextInputProcessor ||
        iid == IID_ITfTextInputProcessorEx) {
        *object = static_cast<ITfTextInputProcessorEx*>(this);
    } else if (iid == IID_ITfKeyEventSink) {
        *object = static_cast<ITfKeyEventSink*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

ULONG TextService::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&references_));
}

ULONG TextService::Release() {
    const auto remaining = InterlockedDecrement(&references_);
    if (remaining == 0) delete this;
    return static_cast<ULONG>(remaining);
}

HRESULT TextService::Activate(ITfThreadMgr* thread_manager, const TfClientId client_id) {
    return ActivateEx(thread_manager, client_id, 0);
}

HRESULT TextService::ActivateEx(ITfThreadMgr* thread_manager,
                                const TfClientId client_id,
                                DWORD) {
    if (thread_manager == nullptr) return E_INVALIDARG;
    if (thread_manager_ != nullptr) return E_UNEXPECTED;
    thread_manager_ = thread_manager;
    thread_manager_->AddRef();
    client_id_ = client_id;

    ITfKeystrokeMgr* keystroke_manager = nullptr;
    HRESULT result = thread_manager_->QueryInterface(IID_PPV_ARGS(&keystroke_manager));
    if (SUCCEEDED(result)) {
        result = keystroke_manager->AdviseKeyEventSink(client_id_, this, TRUE);
        keystroke_manager->Release();
    }
    if (FAILED(result)) {
        Deactivate();
        return result;
    }
    result = initialize_windows();
    if (FAILED(result)) {
        Deactivate();
        return result;
    }
    worker_ = std::jthread([this](const std::stop_token token) { worker_loop(token); });
    return S_OK;
}

HRESULT TextService::Deactivate() {
    if (worker_.joinable()) {
        worker_.request_stop();
        request_ready_.notify_all();
        worker_.join();
    }
    {
        std::lock_guard lock(request_mutex_);
        pending_request_.reset();
    }
    clear_composition();
    destroy_windows();
    if (thread_manager_ != nullptr) {
        ITfKeystrokeMgr* keystroke_manager = nullptr;
        if (SUCCEEDED(thread_manager_->QueryInterface(IID_PPV_ARGS(&keystroke_manager)))) {
            keystroke_manager->UnadviseKeyEventSink(client_id_);
            keystroke_manager->Release();
        }
        thread_manager_->Release();
        thread_manager_ = nullptr;
    }
    client_id_ = TF_CLIENTID_NULL;
    return S_OK;
}

HRESULT TextService::OnSetFocus(BOOL) { return S_OK; }

bool TextService::should_eat_key(const WPARAM key) const noexcept {
    if (key >= 'A' && key <= 'Z') return true;
    if (input_buffer_.empty()) return false;
    return key == VK_BACK || key == VK_ESCAPE || key == VK_SPACE || key == '1';
}

HRESULT TextService::OnTestKeyDown(ITfContext*, WPARAM key, LPARAM, BOOL* eaten) {
    if (eaten == nullptr) return E_POINTER;
    *eaten = should_eat_key(key) ? TRUE : FALSE;
    return S_OK;
}

HRESULT TextService::OnKeyDown(ITfContext* context, WPARAM key, LPARAM, BOOL* eaten) {
    if (eaten == nullptr) return E_POINTER;
    *eaten = should_eat_key(key) ? TRUE : FALSE;
    if (!*eaten) return S_OK;

    if (key >= 'A' && key <= 'Z') {
        input_buffer_.push_back(static_cast<wchar_t>(L'a' + (key - 'A')));
        candidate_.clear();
        ++context_generation_;
        update_candidate_window();
        queue_candidate_request();
    } else if (key == VK_BACK) {
        if (!input_buffer_.empty()) input_buffer_.pop_back();
        candidate_.clear();
        ++context_generation_;
        if (input_buffer_.empty()) clear_composition();
        else {
            update_candidate_window();
            queue_candidate_request();
        }
    } else if (key == VK_ESCAPE) {
        clear_composition();
    } else if ((key == VK_SPACE || key == '1') && context != nullptr && !candidate_.empty()) {
        return commit_candidate(context);
    }
    return S_OK;
}

HRESULT TextService::OnTestKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* eaten) {
    if (eaten == nullptr) return E_POINTER;
    *eaten = FALSE;
    return S_OK;
}

HRESULT TextService::OnKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* eaten) {
    if (eaten == nullptr) return E_POINTER;
    *eaten = FALSE;
    return S_OK;
}

HRESULT TextService::OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) {
    if (eaten == nullptr) return E_POINTER;
    *eaten = FALSE;
    return S_OK;
}

HRESULT TextService::initialize_windows() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&TextService::window_proc), &module)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    WNDCLASSW message_class{};
    message_class.lpfnWndProc = window_proc;
    message_class.hInstance = module;
    message_class.lpszClassName = kMessageClass;
    if (RegisterClassW(&message_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    WNDCLASSW candidate_class = message_class;
    candidate_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    candidate_class.lpszClassName = kCandidateClass;
    if (RegisterClassW(&candidate_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    message_window_ = CreateWindowExW(0, kMessageClass, L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, message_class.hInstance, this);
    if (message_window_ == nullptr) return HRESULT_FROM_WIN32(GetLastError());
    candidate_window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                                        kCandidateClass, L"", WS_POPUP | WS_BORDER,
                                        0, 0, 280, 44, nullptr, nullptr,
                                        message_class.hInstance, this);
    return candidate_window_ != nullptr ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

void TextService::destroy_windows() noexcept {
    if (candidate_window_ != nullptr) DestroyWindow(candidate_window_);
    if (message_window_ != nullptr) DestroyWindow(message_window_);
    candidate_window_ = nullptr;
    message_window_ = nullptr;
}

LRESULT CALLBACK TextService::window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* service = reinterpret_cast<TextService*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<CREATESTRUCTW*>(lparam);
        service = static_cast<TextService*>(creation->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(service));
    }
    if (message == kCandidateReady && service != nullptr) {
        service->handle_candidate_result(reinterpret_cast<CandidateResult*>(lparam));
        return 0;
    }
    if (message == WM_PAINT && service != nullptr) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT bounds{};
        GetClientRect(window, &bounds);
        SetBkMode(dc, TRANSPARENT);
        std::wstring display = service->input_buffer_ + L"  →  ";
        display += service->candidate_.empty() ? L"…" : L"1. " + service->candidate_;
        DrawTextW(dc, display.c_str(), static_cast<int>(display.size()), &bounds,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void TextService::queue_candidate_request() {
    std::lock_guard lock(request_mutex_);
    pending_request_ = PendingRequest{next_request_id_++, context_generation_, input_buffer_};
    request_ready_.notify_one();
}

void TextService::worker_loop(const std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
        PendingRequest request{};
        {
            std::unique_lock lock(request_mutex_);
            request_ready_.wait(lock, stop_token, [this] { return pending_request_.has_value(); });
            if (stop_token.stop_requested()) break;
            request = std::move(*pending_request_);
            pending_request_.reset();
        }
        const protocol::Message message{protocol::MessageType::candidate_request,
                                        request.request_id, request.generation,
                                        utf8_from_wide(request.input)};
        const auto exchanged = ipc::exchange(ipc::kCorePipeName,
                                             protocol::encode_message(message),
                                             std::chrono::milliseconds(100));
        if (!exchanged.status || stop_token.stop_requested()) continue;
        const auto decoded = protocol::decode_message(exchanged.response);
        if (!decoded.validation ||
            decoded.message.type != protocol::MessageType::candidate_response) continue;
        auto result = std::make_unique<CandidateResult>();
        result->request_id = decoded.message.request_id;
        result->generation = decoded.message.context_generation;
        result->candidate = wide_from_utf8(decoded.message.text);
        if (result->candidate.empty()) continue;
        if (PostMessageW(message_window_, kCandidateReady, 0,
                         reinterpret_cast<LPARAM>(result.get()))) {
            result.release();
        }
    }
}

void TextService::handle_candidate_result(CandidateResult* raw_result) {
    std::unique_ptr<CandidateResult> result(raw_result);
    if (result == nullptr || result->generation != context_generation_ ||
        input_buffer_.empty()) return;
    candidate_ = std::move(result->candidate);
    update_candidate_window();
}

void TextService::update_candidate_window() {
    if (candidate_window_ == nullptr || input_buffer_.empty()) return;
    POINT position{};
    GetCursorPos(&position);
    SetWindowPos(candidate_window_, HWND_TOPMOST, position.x + 12, position.y + 20,
                 280, 44, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(candidate_window_, nullptr, TRUE);
}

void TextService::clear_composition() {
    ++context_generation_;
    input_buffer_.clear();
    candidate_.clear();
    if (candidate_window_ != nullptr) ShowWindow(candidate_window_, SW_HIDE);
}

HRESULT TextService::commit_candidate(ITfContext* context) {
    auto* session = new (std::nothrow) CommitEditSession(context, candidate_);
    if (session == nullptr) return E_OUTOFMEMORY;
    HRESULT session_result = E_FAIL;
    const HRESULT request_result = context->RequestEditSession(
        client_id_, session, TF_ES_SYNC | TF_ES_READWRITE, &session_result);
    session->Release();
    if (SUCCEEDED(request_result) && SUCCEEDED(session_result)) clear_composition();
    return FAILED(request_result) ? request_result : session_result;
}

HRESULT create_text_service(REFIID iid, void** object) {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
    auto* service = new (std::nothrow) TextService();
    if (service == nullptr) return E_OUTOFMEMORY;
    const HRESULT result = service->QueryInterface(iid, object);
    service->Release();
    return result;
}

void increment_server_lock() noexcept { InterlockedIncrement(&lock_count); }
void decrement_server_lock() noexcept { InterlockedDecrement(&lock_count); }
long server_lock_count() noexcept { return object_count + lock_count; }

}  // namespace owo::tsf
