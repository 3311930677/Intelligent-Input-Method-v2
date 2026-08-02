#include "text_service.h"

#include "owo/ipc/named_pipe.h"
#include "owo/protocol/messages.h"

#include <d2d1.h>
#include <d2d1helper.h>
#include <dwmapi.h>
#include <dwrite.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

namespace owo::tsf {
namespace {
LONG object_count = 0;
LONG lock_count = 0;
constexpr wchar_t kMessageClass[] = L"OwO.P1.MessageWindow";
constexpr wchar_t kCandidateClass[] = L"OwO.P1.CandidateWindow";
constexpr UINT kCandidateReady = WM_APP + 1;
constexpr float kCandidateHeightDip = 52.0F;
constexpr float kCandidateMinWidthDip = 280.0F;
constexpr float kCandidateMaxWidthDip = 860.0F;
constexpr float kHorizontalPaddingDip = 14.0F;
constexpr float kCandidateGapDip = 6.0F;

template <typename Interface>
void release_interface(Interface*& value) noexcept {
    if (value == nullptr) return;
    value->Release();
    value = nullptr;
}

float measure_text_width(IDWriteFactory* factory,
                         IDWriteTextFormat* format,
                         const std::wstring_view text) {
    if (factory == nullptr || format == nullptr || text.empty()) return 0.0F;
    IDWriteTextLayout* layout = nullptr;
    const HRESULT result = factory->CreateTextLayout(
        text.data(), static_cast<UINT32>(text.size()), format, 4096.0F,
        kCandidateHeightDip, &layout);
    if (FAILED(result)) return 0.0F;
    DWRITE_TEXT_METRICS metrics{};
    const HRESULT metrics_result = layout->GetMetrics(&metrics);
    layout->Release();
    return SUCCEEDED(metrics_result) ? metrics.widthIncludingTrailingWhitespace : 0.0F;
}

int dips_to_pixels(const float dips, const UINT dpi) noexcept {
    return static_cast<int>(std::ceil(dips * static_cast<float>(dpi) / 96.0F));
}

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

class CaretEditSession final : public ITfEditSession {
public:
    CaretEditSession(ITfContext* context, POINT* anchor, bool* valid)
        : context_(context), anchor_(anchor), valid_(valid) {
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
        *valid_ = false;
        TF_SELECTION selection{};
        ULONG fetched = 0;
        HRESULT result = context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1,
                                                &selection, &fetched);
        if (FAILED(result) || fetched != 1 || selection.range == nullptr) return result;
        ITfContextView* view = nullptr;
        result = context_->GetActiveView(&view);
        if (SUCCEEDED(result)) {
            RECT bounds{};
            BOOL clipped = FALSE;
            result = view->GetTextExt(cookie, selection.range, &bounds, &clipped);
            if (SUCCEEDED(result)) {
                anchor_->x = bounds.left;
                anchor_->y = bounds.bottom;
                *valid_ = true;
            }
            view->Release();
        }
        selection.range->Release();
        return result;
    }

private:
    ~CaretEditSession() { context_->Release(); }
    LONG references_{1};
    ITfContext* context_;
    POINT* anchor_;
    bool* valid_;
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
        feedback_requests_.clear();
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
    if (key == VK_BACK || key == VK_ESCAPE) return true;
    if (key == VK_NEXT || key == VK_OEM_6) return has_more_candidates_;
    if (key == VK_PRIOR || key == VK_OEM_4) return candidate_page_ > 0;
    if (key == VK_SPACE) return !candidates_.empty();
    return key >= '1' && key <= '9' &&
           static_cast<std::size_t>(key - '1') < candidates_.size();
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
    if (context != nullptr) update_candidate_anchor(context);

    if (key >= 'A' && key <= 'Z') {
        input_buffer_.push_back(static_cast<wchar_t>(L'a' + (key - 'A')));
        candidate_page_ = 0;
        has_more_candidates_ = false;
        candidates_.clear();
        ++context_generation_;
        update_candidate_window();
        queue_candidate_request();
    } else if (key == VK_BACK) {
        if (!input_buffer_.empty()) input_buffer_.pop_back();
        candidate_page_ = 0;
        has_more_candidates_ = false;
        candidates_.clear();
        ++context_generation_;
        if (input_buffer_.empty()) clear_composition();
        else {
            update_candidate_window();
            queue_candidate_request();
        }
    } else if (key == VK_ESCAPE) {
        clear_composition();
    } else if ((key == VK_NEXT || key == VK_OEM_6) && has_more_candidates_) {
        ++candidate_page_;
        has_more_candidates_ = false;
        candidates_.clear();
        update_candidate_window();
        queue_candidate_request();
    } else if ((key == VK_PRIOR || key == VK_OEM_4) && candidate_page_ > 0) {
        --candidate_page_;
        has_more_candidates_ = false;
        candidates_.clear();
        update_candidate_window();
        queue_candidate_request();
    } else if (context != nullptr && !candidates_.empty()) {
        const std::size_t index = key == VK_SPACE ? 0 : static_cast<std::size_t>(key - '1');
        if (index < candidates_.size()) return commit_candidate(context, index);
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
    candidate_class.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    candidate_class.hbrBackground = nullptr;
    candidate_class.lpszClassName = kCandidateClass;
    if (RegisterClassW(&candidate_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    message_window_ = CreateWindowExW(0, kMessageClass, L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, message_class.hInstance, this);
    if (message_window_ == nullptr) return HRESULT_FROM_WIN32(GetLastError());
    candidate_window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                                        kCandidateClass, L"", WS_POPUP,
                                        0, 0, 280, 52, nullptr, nullptr,
                                        message_class.hInstance, this);
    if (candidate_window_ == nullptr) return HRESULT_FROM_WIN32(GetLastError());
    const HRESULT result = initialize_rendering();
    if (FAILED(result)) return result;
    apply_candidate_window_effects();
    return S_OK;
}

void TextService::destroy_windows() noexcept {
    discard_rendering();
    if (candidate_window_ != nullptr) DestroyWindow(candidate_window_);
    if (message_window_ != nullptr) DestroyWindow(message_window_);
    candidate_window_ = nullptr;
    message_window_ = nullptr;
}

HRESULT TextService::initialize_rendering() {
    HRESULT result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory_);
    if (FAILED(result)) return result;
    result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(&dwrite_factory_));
    if (FAILED(result)) {
        discard_rendering();
        return result;
    }

    result = dwrite_factory_->CreateTextFormat(
        L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0F, L"zh-CN",
        &input_text_format_);
    if (SUCCEEDED(result)) {
        result = dwrite_factory_->CreateTextFormat(
            L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0F, L"zh-CN",
            &candidate_text_format_);
    }
    if (SUCCEEDED(result)) {
        result = dwrite_factory_->CreateTextFormat(
            L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0F, L"zh-CN",
            &label_text_format_);
    }
    if (FAILED(result)) {
        discard_rendering();
        return result;
    }

    input_text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    input_text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    candidate_text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    candidate_text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    label_text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    label_text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    label_text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    return S_OK;
}

HRESULT TextService::ensure_device_resources() {
    if (render_target_ != nullptr) return S_OK;
    if (candidate_window_ == nullptr || d2d_factory_ == nullptr) return E_UNEXPECTED;

    RECT bounds{};
    GetClientRect(candidate_window_, &bounds);
    const UINT dpi = std::max(GetDpiForWindow(candidate_window_), 96U);
    const D2D1_SIZE_U pixel_size = D2D1::SizeU(
        static_cast<UINT32>(std::max(bounds.right - bounds.left, 1L)),
        static_cast<UINT32>(std::max(bounds.bottom - bounds.top, 1L)));
    HRESULT result = d2d_factory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(candidate_window_, pixel_size), &render_target_);
    if (FAILED(result)) return result;
    render_target_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));

    const auto create_brush = [this](const D2D1_COLOR_F color,
                                     ID2D1SolidColorBrush** brush) {
        return render_target_->CreateSolidColorBrush(color, brush);
    };
    result = create_brush(D2D1::ColorF(0x202124, 0.98F), &background_brush_);
    if (SUCCEEDED(result))
        result = create_brush(D2D1::ColorF(0xFFFFFF, 0.14F), &border_brush_);
    if (SUCCEEDED(result))
        result = create_brush(D2D1::ColorF(0xFFFFFF, 0.96F), &text_brush_);
    if (SUCCEEDED(result))
        result = create_brush(D2D1::ColorF(0xFFFFFF, 0.62F), &secondary_text_brush_);
    if (SUCCEEDED(result))
        result = create_brush(D2D1::ColorF(0x8AB4F8, 1.0F), &accent_brush_);
    if (SUCCEEDED(result))
        result = create_brush(D2D1::ColorF(0x8AB4F8, 0.18F), &highlight_brush_);
    if (FAILED(result)) discard_device_resources();
    return result;
}

void TextService::discard_device_resources() noexcept {
    release_interface(highlight_brush_);
    release_interface(accent_brush_);
    release_interface(secondary_text_brush_);
    release_interface(text_brush_);
    release_interface(border_brush_);
    release_interface(background_brush_);
    release_interface(render_target_);
}

void TextService::discard_rendering() noexcept {
    discard_device_resources();
    release_interface(label_text_format_);
    release_interface(candidate_text_format_);
    release_interface(input_text_format_);
    release_interface(dwrite_factory_);
    release_interface(d2d_factory_);
}

void TextService::apply_candidate_window_effects() noexcept {
    if (candidate_window_ == nullptr) return;
    const BOOL dark_mode = TRUE;
    DwmSetWindowAttribute(candidate_window_, DWMWA_USE_IMMERSIVE_DARK_MODE,
                          &dark_mode, sizeof(dark_mode));
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(candidate_window_, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &corners, sizeof(corners));
    const DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_TRANSIENTWINDOW;
    DwmSetWindowAttribute(candidate_window_, DWMWA_SYSTEMBACKDROP_TYPE,
                          &backdrop, sizeof(backdrop));
    const COLORREF border_color = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(candidate_window_, DWMWA_BORDER_COLOR,
                          &border_color, sizeof(border_color));
    const MARGINS margins{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(candidate_window_, &margins);
}

SIZE TextService::desired_candidate_window_size() const {
    const UINT dpi = candidate_window_ == nullptr
                         ? 96U
                         : std::max(GetDpiForWindow(candidate_window_), 96U);
    const float input_width = measure_text_width(dwrite_factory_, input_text_format_,
                                                 input_buffer_);
    float width = kHorizontalPaddingDip * 2.0F + input_width + 34.0F;
    if (candidates_.empty()) {
        width += measure_text_width(dwrite_factory_, candidate_text_format_,
                                    candidate_request_pending_ ? L"正在查找…" : L"无候选") +
                 12.0F;
    } else {
        for (std::size_t index = 0; index < candidates_.size(); ++index) {
            if (index != 0) width += kCandidateGapDip;
            width += 34.0F + measure_text_width(dwrite_factory_, candidate_text_format_,
                                                candidates_[index]);
        }
    }
    width = std::clamp(width, kCandidateMinWidthDip, kCandidateMaxWidthDip);
    return SIZE{dips_to_pixels(width, dpi), dips_to_pixels(kCandidateHeightDip, dpi)};
}

void TextService::render_candidate_window() {
    if (FAILED(ensure_device_resources())) return;
    render_target_->BeginDraw();
    render_target_->SetTransform(D2D1::Matrix3x2F::Identity());
    render_target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    render_target_->Clear(D2D1::ColorF(0x000000, 0.0F));

    const D2D1_SIZE_F size = render_target_->GetSize();
    const D2D1_ROUNDED_RECT card{
        D2D1::RectF(0.5F, 0.5F, size.width - 0.5F, size.height - 0.5F), 10.0F, 10.0F};
    render_target_->FillRoundedRectangle(card, background_brush_);
    render_target_->DrawRoundedRectangle(card, border_brush_, 1.0F);

    float x = kHorizontalPaddingDip;
    const float input_width = measure_text_width(dwrite_factory_, input_text_format_,
                                                 input_buffer_);
    const D2D1_RECT_F input_bounds = D2D1::RectF(
        x, 0.0F, std::min(x + input_width + 2.0F, size.width - kHorizontalPaddingDip),
        size.height);
    render_target_->DrawTextW(input_buffer_.data(), static_cast<UINT32>(input_buffer_.size()),
                              input_text_format_, input_bounds, accent_brush_,
                              D2D1_DRAW_TEXT_OPTIONS_CLIP);
    x = input_bounds.right + 10.0F;
    constexpr wchar_t arrow[] = L"→";
    render_target_->DrawTextW(arrow, 1, candidate_text_format_,
                              D2D1::RectF(x, 0.0F, x + 14.0F, size.height),
                              secondary_text_brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    x += 22.0F;

    if (candidates_.empty()) {
        const std::wstring_view status = candidate_request_pending_ ? L"正在查找…" : L"无候选";
        render_target_->DrawTextW(status.data(), static_cast<UINT32>(status.size()),
                                  candidate_text_format_,
                                  D2D1::RectF(x, 0.0F, size.width - kHorizontalPaddingDip,
                                              size.height),
                                  secondary_text_brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    } else {
        for (std::size_t index = 0; index < candidates_.size(); ++index) {
            if (x >= size.width - kHorizontalPaddingDip) break;
            const float text_width = measure_text_width(
                dwrite_factory_, candidate_text_format_, candidates_[index]);
            const float item_width = std::min(34.0F + text_width,
                                              size.width - kHorizontalPaddingDip - x);
            const D2D1_ROUNDED_RECT item{
                D2D1::RectF(x - 4.0F, 7.0F, x + item_width, size.height - 7.0F),
                7.0F, 7.0F};
            if (index == 0) render_target_->FillRoundedRectangle(item, highlight_brush_);

            const D2D1_ROUNDED_RECT badge{
                D2D1::RectF(x, 14.0F, x + 20.0F, size.height - 14.0F), 5.0F, 5.0F};
            render_target_->FillRoundedRectangle(badge, border_brush_);
            const std::wstring label = std::to_wstring(index + 1);
            render_target_->DrawTextW(label.data(), static_cast<UINT32>(label.size()),
                                      label_text_format_, badge.rect, accent_brush_,
                                      D2D1_DRAW_TEXT_OPTIONS_CLIP);
            render_target_->DrawTextW(
                candidates_[index].data(), static_cast<UINT32>(candidates_[index].size()),
                candidate_text_format_,
                D2D1::RectF(x + 27.0F, 0.0F, x + item_width, size.height), text_brush_,
                D2D1_DRAW_TEXT_OPTIONS_CLIP);
            x += item_width + kCandidateGapDip;
        }
    }

    const HRESULT result = render_target_->EndDraw();
    if (FAILED(result)) {
        discard_device_resources();
        if (candidate_window_ != nullptr) InvalidateRect(candidate_window_, nullptr, FALSE);
    }
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
        BeginPaint(window, &paint);
        if (window == service->candidate_window_) service->render_candidate_window();
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND && service != nullptr &&
        window == service->candidate_window_) {
        return 1;
    }
    if (message == WM_SIZE && service != nullptr &&
        window == service->candidate_window_ && service->render_target_ != nullptr) {
        const HRESULT result = service->render_target_->Resize(
            D2D1::SizeU(static_cast<UINT32>(LOWORD(lparam)),
                        static_cast<UINT32>(HIWORD(lparam))));
        if (FAILED(result)) service->discard_device_resources();
        return 0;
    }
    if (message == WM_DPICHANGED && service != nullptr &&
        window == service->candidate_window_) {
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        service->discard_device_resources();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if ((message == WM_THEMECHANGED || message == WM_DWMCOLORIZATIONCOLORCHANGED) &&
        service != nullptr && window == service->candidate_window_) {
        service->apply_candidate_window_effects();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if (message == WM_DISPLAYCHANGE && service != nullptr &&
        window == service->candidate_window_) {
        service->discard_device_resources();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void TextService::queue_candidate_request() {
    candidate_request_pending_ = true;
    if (candidate_window_ != nullptr) InvalidateRect(candidate_window_, nullptr, TRUE);
    std::lock_guard lock(request_mutex_);
    active_candidate_request_id_ = next_request_id_++;
    pending_request_ = PendingRequest{
        static_cast<std::uint8_t>(protocol::MessageType::candidate_request),
        active_candidate_request_id_, context_generation_, candidate_page_, input_buffer_};
    request_ready_.notify_one();
}

void TextService::queue_commit_feedback(std::wstring candidate) {
    std::lock_guard lock(request_mutex_);
    feedback_requests_.push_back(PendingRequest{
        static_cast<std::uint8_t>(protocol::MessageType::candidate_committed),
        next_request_id_++, context_generation_, 0, std::move(candidate)});
    request_ready_.notify_one();
}

void TextService::worker_loop(const std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
        PendingRequest request{};
        {
            std::unique_lock lock(request_mutex_);
            request_ready_.wait(lock, stop_token, [this] {
                return pending_request_.has_value() || !feedback_requests_.empty();
            });
            if (stop_token.stop_requested()) break;
            if (!feedback_requests_.empty()) {
                request = std::move(feedback_requests_.front());
                feedback_requests_.pop_front();
            } else {
                request = std::move(*pending_request_);
                pending_request_.reset();
            }
        }
        const auto request_type = static_cast<protocol::MessageType>(request.type);
        const protocol::Message message{request_type,
                                        request.request_id, request.generation,
                                        utf8_from_wide(request.input)};
        auto paged_message = message;
        paged_message.page = request.page;
        const auto exchanged = ipc::exchange(ipc::kCorePipeName,
                                             protocol::encode_message(paged_message),
                                             std::chrono::milliseconds(100));
        if (!exchanged.status || stop_token.stop_requested()) continue;
        const auto decoded = protocol::decode_message(exchanged.response);
        if (!decoded.validation || decoded.message.request_id != request.request_id) continue;
        if (request_type == protocol::MessageType::candidate_committed) {
            if (decoded.message.type != protocol::MessageType::acknowledgement) continue;
            continue;
        }
        if (decoded.message.type != protocol::MessageType::candidate_response) continue;
        auto result = std::make_unique<CandidateResult>();
        result->request_id = decoded.message.request_id;
        result->generation = decoded.message.context_generation;
        result->page = decoded.message.page;
        result->has_more = decoded.message.has_more;
        result->candidates.reserve(decoded.message.candidates.size());
        for (const auto& candidate : decoded.message.candidates) {
            auto converted = wide_from_utf8(candidate);
            if (!converted.empty()) result->candidates.push_back(std::move(converted));
        }
        if (PostMessageW(message_window_, kCandidateReady, 0,
                         reinterpret_cast<LPARAM>(result.get()))) {
            result.release();
        }
        if (!decoded.message.model_pending) continue;

        for (int attempt = 0; attempt < 6 && !stop_token.stop_requested(); ++attempt) {
            {
                std::unique_lock lock(request_mutex_);
                const bool interrupted = request_ready_.wait_for(
                    lock, stop_token, std::chrono::milliseconds(10), [this] {
                        return pending_request_.has_value() || !feedback_requests_.empty();
                    });
                if (interrupted || stop_token.stop_requested()) break;
            }
            const protocol::Message update_request{
                protocol::MessageType::candidate_update_request,
                request.request_id, request.generation, {}};
            const auto update_exchange = ipc::exchange(
                ipc::kCorePipeName, protocol::encode_message(update_request),
                std::chrono::milliseconds(25));
            if (!update_exchange.status) break;
            const auto update = protocol::decode_message(update_exchange.response);
            if (!update.validation ||
                update.message.type != protocol::MessageType::candidate_update_response ||
                update.message.request_id != request.request_id ||
                update.message.context_generation != request.generation) break;
            if (update.message.model_pending) continue;
            if (update.message.candidates.empty()) break;
            auto intelligent = std::make_unique<CandidateResult>();
            intelligent->request_id = request.request_id;
            intelligent->generation = request.generation;
            intelligent->page = request.page;
            intelligent->preserve_paging = true;
            for (const auto& candidate : update.message.candidates) {
                auto converted = wide_from_utf8(candidate);
                if (!converted.empty()) intelligent->candidates.push_back(std::move(converted));
            }
            if (!intelligent->candidates.empty() &&
                PostMessageW(message_window_, kCandidateReady, 0,
                             reinterpret_cast<LPARAM>(intelligent.get())))
                intelligent.release();
            break;
        }
    }
}

void TextService::handle_candidate_result(CandidateResult* raw_result) {
    std::unique_ptr<CandidateResult> result(raw_result);
    if (result == nullptr || result->generation != context_generation_ ||
        result->request_id != active_candidate_request_id_ ||
        result->page != candidate_page_ || input_buffer_.empty()) return;
    candidate_request_pending_ = false;
    candidates_ = std::move(result->candidates);
    if (!result->preserve_paging) has_more_candidates_ = result->has_more;
    update_candidate_window();
}

void TextService::update_candidate_window() {
    if (candidate_window_ == nullptr || input_buffer_.empty()) return;
    POINT position = candidate_anchor_;
    if (!candidate_anchor_valid_) GetCursorPos(&position);
    const UINT dpi = std::max(GetDpiForWindow(candidate_window_), 96U);
    const SIZE window_size = desired_candidate_window_size();
    int x = position.x + (candidate_anchor_valid_ ? 0 : dips_to_pixels(12.0F, dpi));
    int y = position.y + (candidate_anchor_valid_ ? dips_to_pixels(6.0F, dpi)
                                                  : dips_to_pixels(20.0F, dpi));

    const HMONITOR monitor = MonitorFromPoint(position, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info{sizeof(monitor_info)};
    if (GetMonitorInfoW(monitor, &monitor_info)) {
        const RECT& work_area = monitor_info.rcWork;
        x = std::min(x, static_cast<int>(work_area.right - window_size.cx));
        if (y + window_size.cy > work_area.bottom) {
            y = position.y - window_size.cy - dips_to_pixels(6.0F, dpi);
        }
        x = std::max(x, static_cast<int>(work_area.left));
        y = std::max(y, static_cast<int>(work_area.top));
    }
    SetWindowPos(candidate_window_, HWND_TOPMOST, x, y, window_size.cx, window_size.cy,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(candidate_window_, nullptr, FALSE);
}

void TextService::update_candidate_anchor(ITfContext* context) {
    auto* session = new (std::nothrow)
        CaretEditSession(context, &candidate_anchor_, &candidate_anchor_valid_);
    if (session == nullptr) return;
    HRESULT session_result = E_FAIL;
    context->RequestEditSession(client_id_, session, TF_ES_SYNC | TF_ES_READ,
                                &session_result);
    session->Release();
}

void TextService::clear_composition() {
    ++context_generation_;
    input_buffer_.clear();
    candidates_.clear();
    candidate_page_ = 0;
    has_more_candidates_ = false;
    candidate_request_pending_ = false;
    candidate_anchor_valid_ = false;
    if (candidate_window_ != nullptr) ShowWindow(candidate_window_, SW_HIDE);
}

HRESULT TextService::commit_candidate(ITfContext* context, const std::size_t index) {
    if (index >= candidates_.size()) return E_INVALIDARG;
    const std::wstring committed = candidates_[index];
    auto* session = new (std::nothrow) CommitEditSession(context, committed);
    if (session == nullptr) return E_OUTOFMEMORY;
    HRESULT session_result = E_FAIL;
    const HRESULT request_result = context->RequestEditSession(
        client_id_, session, TF_ES_SYNC | TF_ES_READWRITE, &session_result);
    session->Release();
    if (SUCCEEDED(request_result) && SUCCEEDED(session_result)) {
        queue_commit_feedback(committed);
        clear_composition();
    }
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
