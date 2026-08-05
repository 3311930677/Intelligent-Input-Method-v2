#pragma once

#include "owo/config/config_store.h"

#include <Windows.h>
#include <msctf.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

struct ID2D1Factory;
struct ID2D1HwndRenderTarget;
struct ID2D1SolidColorBrush;
struct IDWriteFactory;
struct IDWriteTextFormat;

namespace owo::tsf {

inline constexpr CLSID kTextServiceClsid{
    0x6d31c9b1, 0x8978, 0x4f49, {0x89, 0xb4, 0x66, 0xeb, 0x1e, 0x74, 0x15, 0x91}};
inline constexpr GUID kLanguageProfileGuid{
    0x5d9f39c3, 0xbdb4, 0x453c, {0xa7, 0xba, 0xb9, 0xef, 0x82, 0x48, 0x76, 0x29}};
inline constexpr LANGID kSimplifiedChinese = 0x0804;

class TextService final : public ITfTextInputProcessorEx,
                          public ITfKeyEventSink,
                          public ITfThreadMgrEventSink,
                          public ITfThreadFocusSink {
public:
    TextService() noexcept;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE Activate(ITfThreadMgr* thread_manager, TfClientId client_id) override;
    HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr* thread_manager,
                                         TfClientId client_id,
                                         DWORD flags) override;
    HRESULT STDMETHODCALLTYPE Deactivate() override;

    HRESULT STDMETHODCALLTYPE OnSetFocus(BOOL foreground) override;
    HRESULT STDMETHODCALLTYPE OnTestKeyDown(ITfContext* context,
                                            WPARAM key,
                                            LPARAM flags,
                                            BOOL* eaten) override;
    HRESULT STDMETHODCALLTYPE OnKeyDown(ITfContext* context,
                                        WPARAM key,
                                        LPARAM flags,
                                        BOOL* eaten) override;
    HRESULT STDMETHODCALLTYPE OnTestKeyUp(ITfContext* context,
                                          WPARAM key,
                                          LPARAM flags,
                                          BOOL* eaten) override;
    HRESULT STDMETHODCALLTYPE OnKeyUp(ITfContext* context,
                                      WPARAM key,
                                      LPARAM flags,
                                      BOOL* eaten) override;
    HRESULT STDMETHODCALLTYPE OnPreservedKey(ITfContext* context,
                                             REFGUID guid,
                                             BOOL* eaten) override;

    HRESULT STDMETHODCALLTYPE OnInitDocumentMgr(ITfDocumentMgr* document_manager) override;
    HRESULT STDMETHODCALLTYPE OnUninitDocumentMgr(ITfDocumentMgr* document_manager) override;
    HRESULT STDMETHODCALLTYPE OnSetFocus(ITfDocumentMgr* document_manager,
                                         ITfDocumentMgr* previous_document_manager) override;
    HRESULT STDMETHODCALLTYPE OnPushContext(ITfContext* context) override;
    HRESULT STDMETHODCALLTYPE OnPopContext(ITfContext* context) override;

    HRESULT STDMETHODCALLTYPE OnSetThreadFocus() override;
    HRESULT STDMETHODCALLTYPE OnKillThreadFocus() override;

private:
    struct CandidateResult {
        std::uint64_t request_id{};
        std::uint64_t generation{};
        std::uint64_t page{};
        bool has_more{};
        bool expanded{};
        std::uint64_t page_size{5};
        bool preserve_paging{};
        bool request_failed{};
        std::wstring failure_detail;
        std::wstring segmented_input;
        std::vector<std::wstring> candidates;
        std::vector<std::uint64_t> candidate_consumed;
    };
    enum class VoiceUiState : std::uint8_t { idle, listening, final_result, failed, cancelled };
    struct VoiceResult {
        std::uint64_t generation{};
        VoiceUiState state{VoiceUiState::idle};
        std::wstring text;
        std::wstring diagnostic;
    };
    struct PendingRequest {
        std::uint8_t type{};
        std::uint64_t request_id{};
        std::uint64_t generation{};
        std::uint64_t page{};
        std::wstring input;
        bool expanded{};
        bool correction_enabled{true};
    };
    enum class HitKind : std::uint8_t {
        candidate,
        previous_page,
        next_page,
        toggle_expanded,
    voice_input,
     emoji_panel,     // Candidate-bar button that opens the emoji panel.
        emoji_category,  // Panel tab; candidate_index selects the category.
        emoji_glyph,     // Panel grid cell; candidate_index into the page.
        emoji_close,     // Panel close button.
        voice_close,     // Voice panel close button.
        menu_button,  // Candidate-bar button that opens the tool menu.
        menu_emoji,    // Menu row: open the emoji panel.
        menu_settings,// Menu row: launch the settings center app.
   menu_plugin,     // Menu row: toggle plugin at candidate_index.
        menu_close,      // Menu close button.
    };
    struct HitTarget {
        HitKind kind{HitKind::candidate};
        std::size_t candidate_index{};

        bool operator==(const HitTarget&) const = default;
    };
    struct HitRegion {
        RECT bounds{};
        HitTarget target;
    };

    virtual ~TextService();
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    HRESULT initialize_windows();
    void destroy_windows() noexcept;
    HRESULT initialize_rendering();
    HRESULT ensure_device_resources();
    void discard_device_resources() noexcept;
    void discard_rendering() noexcept;
    void apply_candidate_window_effects() noexcept;
    void render_candidate_window();
    [[nodiscard]] SIZE desired_candidate_window_size() const;
    void worker_loop(std::stop_token stop_token);
    void queue_candidate_request();
    void schedule_candidate_request(bool reset_retry);
    void queue_commit_feedback(std::wstring candidate);
    void start_voice_input(ITfContext* context);
    void cancel_voice_input(bool hide_window);
    void voice_worker_loop(std::stop_token stop_token, std::uint64_t generation,
                           std::string owner);
    void handle_voice_result(VoiceResult* result);
    HRESULT commit_voice_text(std::wstring text);
    void refresh_shortcut_config(bool force = false);
    [[nodiscard]] bool shortcut_matches(std::string_view shortcut, WPARAM key) const;
    void handle_candidate_result(CandidateResult* result);
    void poll_candidate_watchdog();
    [[nodiscard]] float emoji_tab_row_width() const;
void update_candidate_window();
 void open_emoji_panel();
    void close_emoji_panel();
    void draw_emoji_panel(UINT dpi);
    std::vector<std::wstring> current_panel_glyphs() const;
    HRESULT commit_text_to_focus(std::wstring text);
    void open_menu();
    void close_menu();
    void draw_menu_panel(UINT dpi);
    void refresh_menu_plugins();
    void toggle_menu_plugin(std::size_t index);
    void launch_settings_center();
    std::wstring tsf_module_directory() const;
void change_candidate_page(int direction);
    void scroll_expanded_candidates(int rows);
    void invoke_hit_target(const HitTarget& target);
    void defer_candidate_selection(std::size_t index, ITfContext* context);
    void clear_deferred_candidate_selection() noexcept;
    [[nodiscard]] std::optional<HitTarget> hit_test(POINT point) const;
    void update_candidate_anchor(ITfContext* context);
    void clear_composition();
    [[nodiscard]] bool should_eat_key(WPARAM key) const noexcept;
    HRESULT commit_candidate(ITfContext* context, std::size_t index);
    HRESULT commit_raw_input(ITfContext* context);
    HRESULT commit_text(ITfContext* context, std::wstring text);
    HRESULT commit_candidate_from_window(std::size_t index);

    LONG references_{1};
    ITfThreadMgr* thread_manager_{nullptr};
    TfClientId client_id_{TF_CLIENTID_NULL};
    DWORD thread_manager_event_sink_cookie_{TF_INVALID_COOKIE};
    DWORD thread_focus_sink_cookie_{TF_INVALID_COOKIE};
    HWND message_window_{nullptr};
    HWND candidate_window_{nullptr};
    ID2D1Factory* d2d_factory_{nullptr};
    IDWriteFactory* dwrite_factory_{nullptr};
    IDWriteTextFormat* input_text_format_{nullptr};
    IDWriteTextFormat* candidate_text_format_{nullptr};
    IDWriteTextFormat* label_text_format_{nullptr};
    // Larger, centred format used to render colourful emoji/symbol glyphs in
    // the panel grid (paired with D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT).
    IDWriteTextFormat* emoji_text_format_{nullptr};
  // Same size as emoji_text_format_ but uses a monochrome text font so
    // characters with an emoji presentation (e.g. ▶ ◀ ▼) render as plain
    // symbols rather than colourful pictographs.
    IDWriteTextFormat* symbol_text_format_{nullptr};
    ID2D1HwndRenderTarget* render_target_{nullptr};
    ID2D1SolidColorBrush* background_brush_{nullptr};
    ID2D1SolidColorBrush* border_brush_{nullptr};
    ID2D1SolidColorBrush* text_brush_{nullptr};
    ID2D1SolidColorBrush* secondary_text_brush_{nullptr};
    ID2D1SolidColorBrush* accent_brush_{nullptr};
    ID2D1SolidColorBrush* highlight_brush_{nullptr};
    ID2D1SolidColorBrush* strict_background_brush_{nullptr};
    ID2D1SolidColorBrush* strict_accent_brush_{nullptr};
    ID2D1SolidColorBrush* strict_highlight_brush_{nullptr};
    std::wstring input_buffer_;
    std::wstring segmented_input_;
    // Caret position within input_buffer_ (not within segmented_input_, which
    // contains apostrophe separators). Left/Right move this caret instead of
    // letting the host move the document caret; typing/Backspace edit here.
    std::size_t input_caret_{0};
    std::vector<std::wstring> candidates_;
    std::vector<std::uint64_t> candidate_consumed_;
    std::wstring candidate_failure_detail_;
    std::vector<HitRegion> hit_regions_;
    std::optional<HitTarget> hovered_target_;
    std::optional<HitTarget> pressed_target_;
    std::optional<std::wstring> deferred_candidate_text_;
    ITfContext* deferred_candidate_context_{nullptr};
    std::uint64_t context_generation_{0};
    std::uint64_t next_request_id_{1};
    std::uint64_t active_candidate_request_id_{0};
    std::uint64_t candidate_page_{0};
    std::uint64_t candidate_page_size_{5};
    bool has_more_candidates_{false};
    bool candidate_request_pending_{false};
    bool candidate_request_failed_{false};
    std::uint8_t candidate_retry_count_{0};
    // Watchdog state guarding against the candidate window sticking on the
    // "正在查找…" placeholder forever (see poll_candidate_watchdog()).
    ULONGLONG candidate_request_started_at_{0};
    bool candidate_watchdog_reissued_{false};
    bool candidates_expanded_{false};
    std::size_t expanded_scroll_row_{0};
    // Emoji/symbol panel state. When emoji_panel_open_ is true the candidate
    // window is repurposed to render the panel instead of the pinyin row.
    bool emoji_panel_open_{false};
    std::size_t emoji_panel_category_{0};
    std::size_t emoji_panel_scroll_{0};
    // ASCII search term typed while the panel is open (English keywords).
    std::string emoji_panel_search_;
    // Glyphs currently laid out in the panel grid (UTF-16), in draw order, so
    // an emoji_glyph hit target can map its index back to a glyph to commit.
    std::vector<std::wstring> emoji_panel_page_glyphs_;
    // Tool menu state (opened from the candidate-bar menu button). Lists the
    // emoji entry, the settings-center entry, and one row per installed plugin
    // that can be toggled on/off in place.
    bool menu_open_{false};
    struct MenuPlugin {
        std::wstring id;
        std::wstring name;
        bool enabled{false};
    };
    std::vector<MenuPlugin> menu_plugins_;
    config::ConfigStore shortcut_config_store_;
    config::AppConfig shortcut_config_;
    ULONGLONG next_shortcut_config_refresh_{0};
    bool shortcut_config_initialized_{false};
    bool correction_enabled_{true};
    bool chinese_mode_{true};
    bool foreground_focus_{true};
    // Track a solo Shift press for the "tap Shift to switch language" gesture.
    // shift_pending_toggle_ is true from the moment Shift goes down as long as
    // no other key has been observed while Shift is held; shift_press_tick_
    // records when Shift went down so we can enforce a short tap window.
    bool shift_pending_toggle_{false};
    ULONGLONG shift_press_tick_{0};
    bool voice_visible_{false};
    bool voice_active_{false};
    // Latched once the Core reports that no speech backend is wired up
    // ("voice broker is unavailable"). Retrying can never succeed in that
    // configuration, so the button stops spawning worker threads and the panel
  // shows a terminal, non-retriable notice instead of flapping open/closed.
    bool voice_unavailable_{false};
    VoiceUiState voice_state_{VoiceUiState::idle};
    std::uint64_t voice_generation_{};
    std::wstring voice_text_;
    std::wstring voice_diagnostic_;
    std::string voice_owner_;
    ITfDocumentMgr* voice_document_manager_{nullptr};
    POINT candidate_anchor_{};
    bool candidate_anchor_valid_{false};
    std::mutex request_mutex_;
    std::condition_variable_any request_ready_;
    std::optional<PendingRequest> pending_request_;
    std::deque<PendingRequest> feedback_requests_;
    std::jthread worker_;
    std::jthread voice_worker_;
};

HRESULT create_text_service(REFIID iid, void** object);
void increment_server_lock() noexcept;
void decrement_server_lock() noexcept;
[[nodiscard]] long server_lock_count() noexcept;

}  // namespace owo::tsf
