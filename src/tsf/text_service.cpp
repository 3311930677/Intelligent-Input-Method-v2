#include "text_service.h"

#include <new>

namespace owo::tsf {
namespace {
LONG object_count = 0;
LONG lock_count = 0;
}

TextService::TextService() noexcept {
    InterlockedIncrement(&object_count);
}

HRESULT TextService::QueryInterface(REFIID iid, void** object) {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
    if (iid == IID_IUnknown || iid == IID_ITfTextInputProcessor ||
        iid == IID_ITfTextInputProcessorEx) {
        *object = static_cast<ITfTextInputProcessorEx*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG TextService::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&references_));
}

ULONG TextService::Release() {
    const auto remaining = InterlockedDecrement(&references_);
    if (remaining == 0) {
        InterlockedDecrement(&object_count);
        delete this;
    }
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
    return S_OK;
}

HRESULT TextService::Deactivate() {
    if (thread_manager_ != nullptr) {
        thread_manager_->Release();
        thread_manager_ = nullptr;
    }
    client_id_ = TF_CLIENTID_NULL;
    return S_OK;
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
