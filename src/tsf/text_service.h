#pragma once

#include <Windows.h>
#include <msctf.h>

namespace owo::tsf {

inline constexpr CLSID kTextServiceClsid{
    0x6d31c9b1, 0x8978, 0x4f49, {0x89, 0xb4, 0x66, 0xeb, 0x1e, 0x74, 0x15, 0x91}};
inline constexpr GUID kLanguageProfileGuid{
    0x5d9f39c3, 0xbdb4, 0x453c, {0xa7, 0xba, 0xb9, 0xef, 0x82, 0x48, 0x76, 0x29}};
inline constexpr LANGID kSimplifiedChinese = 0x0804;

class TextService final : public ITfTextInputProcessorEx {
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

private:
    ~TextService() = default;
    LONG references_{1};
    ITfThreadMgr* thread_manager_{nullptr};
    TfClientId client_id_{TF_CLIENTID_NULL};
};

HRESULT create_text_service(REFIID iid, void** object);
void increment_server_lock() noexcept;
void decrement_server_lock() noexcept;
[[nodiscard]] long server_lock_count() noexcept;

}  // namespace owo::tsf
