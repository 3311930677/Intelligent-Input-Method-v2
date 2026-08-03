#include "owo/voice/voice_protocol.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#include <sapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::system_clock;

struct Arguments {
    bool stdio_mode{};
    bool once_mode{};
    std::string language{"zh-CN"};
    std::uint32_t timeout_ms{10'000};
};

enum class RecognitionState {
    success,
    timeout,
    cancelled,
    error,
};

struct RecognitionOutcome {
    RecognitionState state{RecognitionState::error};
    std::string text;
    std::string diagnostic;
};

class ComApartment {
public:
    ComApartment() : status_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
    ~ComApartment() {
        if (SUCCEEDED(status_)) CoUninitialize();
    }

    [[nodiscard]] HRESULT status() const noexcept { return status_; }

private:
    HRESULT status_;
};

template <typename Interface>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    ~ComPtr() { reset(); }

    Interface* operator->() const noexcept { return value_; }
    Interface* get() const noexcept { return value_; }
    Interface** put() noexcept {
        reset();
        return &value_;
    }

private:
    void reset() noexcept {
        if (value_ != nullptr) value_->Release();
        value_ = nullptr;
    }

    Interface* value_{};
};

bool parse_uint32(const std::string_view value, std::uint32_t& output) {
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}

bool valid_language_argument(const std::string_view language) {
    if (language.size() < 2 || language.size() > 35 || language.front() == '-' ||
        language.back() == '-') return false;
    bool previous_dash = false;
    for (const unsigned char byte : language) {
        const bool valid = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                           (byte >= '0' && byte <= '9') || byte == '-';
        if (!valid || (byte == '-' && previous_dash)) return false;
        previous_dash = byte == '-';
    }
    return true;
}

bool parse_arguments(const int argc, char** argv, Arguments& arguments) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--stdio") {
            arguments.stdio_mode = true;
        } else if (argument == "--once") {
            arguments.once_mode = true;
        } else if (argument == "--language" && index + 1 < argc) {
            arguments.language = argv[++index];
        } else if (argument == "--timeout-ms" && index + 1 < argc) {
            if (!parse_uint32(argv[++index], arguments.timeout_ms)) return false;
        } else {
            return false;
        }
    }
    return arguments.stdio_mode != arguments.once_mode &&
           valid_language_argument(arguments.language) && arguments.timeout_ms >= 100 &&
           arguments.timeout_ms <= owo::voice::kMaximumRecognitionTimeoutMs;
}

std::wstring widen_ascii(const std::string_view input) {
    if (std::any_of(input.begin(), input.end(), [](const unsigned char value) {
            return value > 0x7fU;
        })) return {};
    return std::wstring(input.begin(), input.end());
}

std::string utf8_from_wide(const std::wstring_view input) {
    if (input.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
                                         static_cast<int>(input.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) return {};
    std::string output(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
                            static_cast<int>(input.size()), output.data(), size,
                            nullptr, nullptr) != size) return {};
    return output;
}

std::string hresult_diagnostic(const std::string_view operation, const HRESULT result) {
    std::ostringstream stream;
    stream << operation << " failed with HRESULT 0x" << std::hex << std::uppercase
           << static_cast<std::uint32_t>(result);
    return stream.str();
}

HRESULT find_sapi_token(const wchar_t* category_id, const wchar_t* required_attributes,
                        ComPtr<ISpObjectToken>& token) {
    ComPtr<ISpObjectTokenCategory> category;
    HRESULT status = CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr,
                                      CLSCTX_INPROC_SERVER, IID_ISpObjectTokenCategory,
                                      reinterpret_cast<void**>(category.put()));
    if (FAILED(status)) return status;
    status = category->SetId(category_id, FALSE);
    if (FAILED(status)) return status;
    ComPtr<IEnumSpObjectTokens> tokens;
    status = category->EnumTokens(required_attributes, nullptr, tokens.put());
    if (FAILED(status)) return status;
    ULONG fetched = 0;
    status = tokens->Next(1, token.put(), &fetched);
    return status == S_OK && fetched == 1 ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

void clear_speech_event(SPEVENT& event) {
    if ((event.elParamType == SPET_LPARAM_IS_TOKEN ||
         event.elParamType == SPET_LPARAM_IS_OBJECT) && event.lParam != 0) {
        reinterpret_cast<IUnknown*>(event.lParam)->Release();
    } else if (event.elParamType == SPET_LPARAM_IS_STRING && event.lParam != 0) {
        CoTaskMemFree(reinterpret_cast<void*>(event.lParam));
    }
    event = {};
}

bool recognizer_attributes(const std::string_view language, std::wstring& attributes,
                           std::string& diagnostic) {
    const auto wide_language = widen_ascii(language);
    if (wide_language.empty()) {
        diagnostic = "language tag must contain ASCII BCP-47 characters";
        return false;
    }
    const LCID locale = LocaleNameToLCID(wide_language.c_str(), LOCALE_ALLOW_NEUTRAL_NAMES);
    if (locale == 0) {
        diagnostic = "Windows does not recognize language tag " + std::string(language);
        return false;
    }
    wchar_t buffer[32]{};
    if (swprintf_s(buffer, L"Language=%x", static_cast<unsigned int>(LANGIDFROMLCID(locale))) < 0) {
        diagnostic = "cannot prepare SAPI recognizer language filter";
        return false;
    }
    attributes = buffer;
    return true;
}

std::string result_text(ISpRecoResult* result) {
    if (result == nullptr) return {};
    wchar_t* raw = nullptr;
    const HRESULT status = result->GetText(static_cast<ULONG>(SP_GETWHOLEPHRASE),
                                            static_cast<ULONG>(SP_GETWHOLEPHRASE),
                                            TRUE, &raw, nullptr);
    if (FAILED(status) || raw == nullptr) return {};
    const std::wstring_view text(raw);
    auto converted = utf8_from_wide(text);
    CoTaskMemFree(raw);
    return converted;
}

RecognitionOutcome recognize_once(
    const std::string_view language, const std::chrono::milliseconds timeout,
    const std::atomic_bool& cancelled,
    const std::function<void(const std::string&)>& on_partial) {
    ComApartment apartment;
    if (FAILED(apartment.status())) {
        return {RecognitionState::error, {},
                hresult_diagnostic("CoInitializeEx", apartment.status())};
    }

    std::wstring attributes;
    std::string diagnostic;
    if (!recognizer_attributes(language, attributes, diagnostic)) {
        return {RecognitionState::error, {}, std::move(diagnostic)};
    }

    ComPtr<ISpObjectToken> recognizer_token;
    HRESULT status = find_sapi_token(SPCAT_RECOGNIZERS, attributes.c_str(),
                                     recognizer_token);
    if (FAILED(status)) {
        return {RecognitionState::error, {},
                "no Windows SAPI recognizer is installed for " + std::string(language)};
    }

    ComPtr<ISpRecognizer> recognizer;
    status = CoCreateInstance(CLSID_SpInprocRecognizer, nullptr, CLSCTX_INPROC_SERVER,
                              IID_ISpRecognizer,
                              reinterpret_cast<void**>(recognizer.put()));
    if (FAILED(status)) {
        return {RecognitionState::error, {},
                hresult_diagnostic("CoCreateInstance(CLSID_SpInprocRecognizer)", status)};
    }
    status = recognizer->SetRecognizer(recognizer_token.get());
    if (FAILED(status)) {
        return {RecognitionState::error, {},
                hresult_diagnostic("ISpRecognizer::SetRecognizer", status)};
    }

    ComPtr<ISpObjectToken> audio_token;
    status = find_sapi_token(SPCAT_AUDIOIN, nullptr, audio_token);
    ComPtr<ISpAudio> audio_input;
    if (SUCCEEDED(status)) {
        status = audio_token->CreateInstance(nullptr, CLSCTX_INPROC_SERVER, IID_ISpAudio,
                                              reinterpret_cast<void**>(audio_input.put()));
    }
    if (FAILED(status)) {
        return {RecognitionState::error, {},
                "no default microphone is available to Windows SAPI"};
    }
    status = recognizer->SetInput(audio_input.get(), TRUE);
    if (FAILED(status)) {
        return {RecognitionState::error, {},
                hresult_diagnostic("ISpRecognizer::SetInput", status)};
    }

    ComPtr<ISpRecoContext> context;
    status = recognizer->CreateRecoContext(context.put());
    if (FAILED(status)) {
        return {RecognitionState::error, {},
                hresult_diagnostic("ISpRecognizer::CreateRecoContext", status)};
    }
    status = context->SetNotifyWin32Event();
    if (FAILED(status)) {
        return {RecognitionState::error, {},
                hresult_diagnostic("ISpRecoContext::SetNotifyWin32Event", status)};
    }
    constexpr ULONGLONG interests = SPFEI(SPEI_HYPOTHESIS) | SPFEI(SPEI_RECOGNITION) |
                                    SPFEI(SPEI_FALSE_RECOGNITION);
    status = context->SetInterest(interests, interests);
    if (FAILED(status)) {
        return {RecognitionState::error, {},
                hresult_diagnostic("ISpRecoContext::SetInterest", status)};
    }

    ComPtr<ISpRecoGrammar> grammar;
    status = context->CreateGrammar(1, grammar.put());
    if (SUCCEEDED(status)) status = grammar->LoadDictation(nullptr, SPLO_STATIC);
    if (SUCCEEDED(status)) status = grammar->SetDictationState(SPRS_ACTIVE);
    if (SUCCEEDED(status)) status = recognizer->SetRecoState(SPRST_ACTIVE_ALWAYS);
    if (FAILED(status)) {
        return {RecognitionState::error, {},
                "the installed SAPI recognizer does not support dictation for " +
                    std::string(language)};
    }

    const HANDLE event_handle = context->GetNotifyEventHandle();
    if (event_handle == INVALID_HANDLE_VALUE || event_handle == nullptr) {
        return {RecognitionState::error, {}, "SAPI did not provide a recognition event"};
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        if (cancelled.load(std::memory_order_acquire)) {
            grammar->SetDictationState(SPRS_INACTIVE);
                return {RecognitionState::cancelled, {}, "recognition cancelled"};
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            grammar->SetDictationState(SPRS_INACTIVE);
                return {RecognitionState::timeout, {}, "recognition timed out"};
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const DWORD wait_ms = static_cast<DWORD>((std::min)(remaining.count(), 100LL));
        const DWORD waited = WaitForSingleObject(event_handle, wait_ms);
        if (waited == WAIT_TIMEOUT) continue;
        if (waited != WAIT_OBJECT_0) {
            grammar->SetDictationState(SPRS_INACTIVE);
                return {RecognitionState::error, {}, "waiting for SAPI recognition failed"};
        }

        SPEVENT event{};
        ULONG fetched = 0;
        while (context->GetEvents(1, &event, &fetched) == S_OK && fetched == 1) {
            if (event.eEventId == SPEI_HYPOTHESIS || event.eEventId == SPEI_RECOGNITION) {
                const auto text = result_text(reinterpret_cast<ISpRecoResult*>(event.lParam));
                const bool final = event.eEventId == SPEI_RECOGNITION;
                clear_speech_event(event);
                if (text.empty()) continue;
                if (final) {
                    grammar->SetDictationState(SPRS_INACTIVE);
                                return {RecognitionState::success, text, {}};
                }
                on_partial(text);
                continue;
            }
            clear_speech_event(event);
        }
    }
}

bool read_exact(std::istream& input, char* data, const std::size_t size) {
    input.read(data, static_cast<std::streamsize>(size));
    return input.gcount() == static_cast<std::streamsize>(size);
}

bool read_message(owo::voice::VoiceMessage& message, std::string& diagnostic) {
    std::array<unsigned char, 4> prefix{};
    if (!read_exact(std::cin, reinterpret_cast<char*>(prefix.data()), prefix.size())) {
        diagnostic = std::cin.eof() ? "voice broker closed the transport"
                                    : "cannot read voice protocol frame";
        return false;
    }
    const std::uint32_t size = static_cast<std::uint32_t>(prefix[0]) |
        (static_cast<std::uint32_t>(prefix[1]) << 8U) |
        (static_cast<std::uint32_t>(prefix[2]) << 16U) |
        (static_cast<std::uint32_t>(prefix[3]) << 24U);
    if (size == 0 || size > owo::protocol::kMaximumPayloadBytes) {
        diagnostic = "invalid voice protocol frame size";
        return false;
    }
    std::string bytes(size, '\0');
    if (!read_exact(std::cin, bytes.data(), bytes.size())) {
        diagnostic = "truncated voice protocol frame";
        return false;
    }
    auto decoded = owo::voice::decode_voice_message(bytes);
    if (!decoded.validation) {
        diagnostic = decoded.validation.message;
        return false;
    }
    message = std::move(decoded.message);
    return true;
}

bool write_message(const owo::voice::VoiceMessage& message, std::mutex& mutex) {
    const auto bytes = owo::voice::encode_voice_message(message);
    if (bytes.empty()) return false;
    std::array<char, 4> prefix{
        static_cast<char>(bytes.size() & 0xffU),
        static_cast<char>((bytes.size() >> 8U) & 0xffU),
        static_cast<char>((bytes.size() >> 16U) & 0xffU),
        static_cast<char>((bytes.size() >> 24U) & 0xffU),
    };
    std::lock_guard lock(mutex);
    std::cout.write(prefix.data(), static_cast<std::streamsize>(prefix.size()));
    std::cout.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    std::cout.flush();
    return std::cout.good();
}

owo::voice::VoiceMessage response_for(const std::uint64_t request_id,
                                      const owo::voice::VoiceMessageType type,
                                      const owo::voice::VoiceStatus status) {
    owo::voice::VoiceMessage response;
    response.type = type;
    response.status = status;
    response.request_id = request_id;
    response.plugin_id = std::string(owo::voice::kVoicePluginId);
    return response;
}

bool same_grant(const owo::voice::CapabilityGrant& left,
                const owo::voice::CapabilityGrant& right) {
    return left.grant_id == right.grant_id && left.subject == right.subject &&
           left.scope == right.scope && left.expires_at_unix_ms == right.expires_at_unix_ms &&
           left.max_uses == right.max_uses && left.risk_level == right.risk_level &&
           left.capabilities == right.capabilities;
}

struct GrantUse {
    owo::voice::CapabilityGrant grant;
    std::uint32_t uses{};
};

bool authorize_grant(const owo::voice::CapabilityGrant& grant,
                     std::map<std::string, GrantUse>& grants, std::string& diagnostic) {
    const std::vector<std::string> required{std::string(owo::voice::kMicrophoneCapability)};
    if (grant.subject != owo::voice::kVoicePluginId ||
        grant.scope != owo::voice::kActiveCompositionScope ||
        grant.risk_level != owo::voice::RiskLevel::r3 || grant.capabilities != required) {
        diagnostic = "grant does not match the voice input R3 capability boundary";
        return false;
    }
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now().time_since_epoch()).count();
    if (now <= 0 || grant.expires_at_unix_ms <= static_cast<std::uint64_t>(now) ||
        grant.expires_at_unix_ms > static_cast<std::uint64_t>(now) + 5U * 60U * 1000U) {
        diagnostic = "grant is expired or exceeds the five-minute lifetime";
        return false;
    }
    auto [entry, inserted] = grants.try_emplace(grant.grant_id, GrantUse{grant, 0});
    if (!inserted && !same_grant(entry->second.grant, grant)) {
        diagnostic = "grant identifier was reused with different constraints";
        return false;
    }
    if (entry->second.uses >= grant.max_uses) {
        diagnostic = "grant use limit has been exhausted";
        return false;
    }
    ++entry->second.uses;
    return true;
}

int run_protocol() {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    std::mutex output_mutex;
    const std::vector<std::string> capabilities{
        std::string(owo::voice::kMicrophoneCapability),
        std::string(owo::voice::kTranscribeCapability)};
    auto hello = response_for(1, owo::voice::VoiceMessageType::hello_request,
                              owo::voice::VoiceStatus::success);
    hello.capabilities = capabilities;
    if (!write_message(hello, output_mutex)) return 20;

    owo::voice::VoiceMessage handshake;
    std::string transport_diagnostic;
    if (!read_message(handshake, transport_diagnostic) ||
        handshake.type != owo::voice::VoiceMessageType::hello_response ||
        handshake.request_id != hello.request_id ||
        handshake.plugin_id != owo::voice::kVoicePluginId ||
        handshake.capabilities != capabilities) return 21;

    std::jthread worker;
    std::atomic_bool cancelled{false};
    std::atomic_bool active{false};
    std::atomic_uint64_t active_request{0};
    std::map<std::string, GrantUse> grants;

    const auto send_error = [&](const std::uint64_t request_id,
                                const owo::voice::VoiceStatus status,
                                std::string diagnostic) {
        auto error = response_for(request_id, owo::voice::VoiceMessageType::error_response, status);
        error.diagnostic = std::move(diagnostic);
        return write_message(error, output_mutex);
    };
    const auto reap_worker = [&] {
        if (!active.load(std::memory_order_acquire) && worker.joinable()) worker.join();
    };

    for (;;) {
        reap_worker();
        owo::voice::VoiceMessage request;
        if (!read_message(request, transport_diagnostic)) {
            cancelled.store(true, std::memory_order_release);
            if (worker.joinable()) worker.join();
            return 22;
        }
        if (request.plugin_id != owo::voice::kVoicePluginId) {
            cancelled.store(true, std::memory_order_release);
            if (worker.joinable()) worker.join();
            return 23;
        }

        if (request.type == owo::voice::VoiceMessageType::shutdown_request) {
            cancelled.store(true, std::memory_order_release);
            if (worker.joinable()) worker.join();
            auto acknowledgement = response_for(request.request_id,
                owo::voice::VoiceMessageType::acknowledgement,
                owo::voice::VoiceStatus::success);
            return write_message(acknowledgement, output_mutex) ? 0 : 24;
        }
        if (request.type == owo::voice::VoiceMessageType::cancel_request) {
            if (!active.load(std::memory_order_acquire) ||
                request.target_request_id != active_request.load(std::memory_order_acquire)) {
                if (!send_error(request.request_id, owo::voice::VoiceStatus::invalid_request,
                                "cancel target is not active")) return 25;
                continue;
            }
            cancelled.store(true, std::memory_order_release);
            auto acknowledgement = response_for(request.request_id,
                owo::voice::VoiceMessageType::acknowledgement,
                owo::voice::VoiceStatus::success);
            if (!write_message(acknowledgement, output_mutex)) return 26;
            continue;
        }
        if (request.type != owo::voice::VoiceMessageType::start_request) {
            if (!send_error(request.request_id, owo::voice::VoiceStatus::invalid_request,
                            "expected a start, cancel, or shutdown request")) return 27;
            continue;
        }
        if (active.load(std::memory_order_acquire)) {
            if (!send_error(request.request_id, owo::voice::VoiceStatus::invalid_request,
                            "a recognition session is already active")) return 28;
            continue;
        }
        std::string authorization_diagnostic;
        if (!authorize_grant(request.grant, grants, authorization_diagnostic)) {
            if (!send_error(request.request_id, owo::voice::VoiceStatus::permission_denied,
                            std::move(authorization_diagnostic))) return 29;
            continue;
        }

        cancelled.store(false, std::memory_order_release);
        active.store(true, std::memory_order_release);
        active_request.store(request.request_id, std::memory_order_release);
        auto acknowledgement = response_for(request.request_id,
            owo::voice::VoiceMessageType::acknowledgement,
            owo::voice::VoiceStatus::success);
        if (!write_message(acknowledgement, output_mutex)) {
            active_request.store(0, std::memory_order_release);
            active.store(false, std::memory_order_release);
            return 30;
        }

        worker = std::jthread([&, request] {
            const auto partial = [&](const std::string& text) {
                auto message = response_for(request.request_id,
                    owo::voice::VoiceMessageType::partial_result,
                    owo::voice::VoiceStatus::success);
                message.text = text;
                write_message(message, output_mutex);
            };
            const auto result = recognize_once(request.language,
                std::chrono::milliseconds(request.timeout_ms), cancelled, partial);
            if (result.state == RecognitionState::success) {
                auto final = response_for(request.request_id,
                    owo::voice::VoiceMessageType::final_result,
                    owo::voice::VoiceStatus::success);
                final.text = result.text;
                write_message(final, output_mutex);
            } else {
                owo::voice::VoiceStatus status = owo::voice::VoiceStatus::recognizer_error;
                if (result.state == RecognitionState::timeout)
                    status = owo::voice::VoiceStatus::timeout;
                else if (result.state == RecognitionState::cancelled)
                    status = owo::voice::VoiceStatus::cancelled;
                send_error(request.request_id, status, result.diagnostic);
            }
            active_request.store(0, std::memory_order_release);
            active.store(false, std::memory_order_release);
        });
    }
}

int run_once(const Arguments& arguments) {
    // Recognition results are UTF-8. Make a directly launched Windows console decode them as
    // UTF-8; protocol mode remains binary and is not affected by console code pages.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    const std::atomic_bool cancelled{false};
    const auto result = recognize_once(arguments.language,
        std::chrono::milliseconds(arguments.timeout_ms), cancelled,
        [](const std::string& text) { std::cerr << "partial: " << text << '\n'; });
    if (result.state == RecognitionState::success) {
        std::cout << result.text << '\n';
        return 0;
    }
    std::cerr << result.diagnostic << '\n';
    if (result.state == RecognitionState::timeout) return 3;
    if (result.state == RecognitionState::cancelled) return 4;
    return 5;
}

}  // namespace

int main(const int argc, char** argv) {
    Arguments arguments;
    if (!parse_arguments(argc, argv, arguments)) {
        std::cerr << "usage: owo_voice_input_plugin (--stdio | --once) "
                     "[--language zh-CN] [--timeout-ms 10000]\n";
        return 2;
    }
    return arguments.stdio_mode ? run_protocol() : run_once(arguments);
}
