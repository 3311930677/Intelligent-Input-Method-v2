#include "owo/ipc/named_pipe.h"
#include "owo/config/config_monitor.h"
#include "owo/model/model_backend.h"
#include "owo/model/model_protocol.h"

#include "owo/engine/candidate_generator.h"
#include "owo/engine/full_pinyin_schema.h"
#include "owo/engine/lexicon.h"
#include "owo/engine/user_frequency.h"

#include "owo/protocol/messages.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <chrono>
#include <future>
#include <iostream>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace owo::ipc {
namespace {

std::string_view voice_state_name(const VoiceSessionState state) noexcept {
    switch (state) {
        case VoiceSessionState::idle: return "idle";
        case VoiceSessionState::listening: return "listening";
        case VoiceSessionState::final_result: return "final";
        case VoiceSessionState::failed: return "failed";
        case VoiceSessionState::cancelled: return "cancelled";
    }
    return "failed";
}

protocol::ValidationResult io_error(const char* operation) {
    const DWORD error = GetLastError();
    const auto code = error == ERROR_SEM_TIMEOUT || error == ERROR_TIMEOUT
                          ? protocol::ErrorCode::timeout
                          : protocol::ErrorCode::transport_unavailable;
    return {code,
            std::string(operation) + " failed with Win32 error " +
                std::to_string(error)};
}

using Deadline = std::chrono::steady_clock::time_point;

DWORD remaining_milliseconds(const Deadline deadline) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0) return 0;
    return static_cast<DWORD>((std::min)(remaining.count(),
                                        static_cast<long long>(INFINITE - 1)));
}

bool overlapped_transfer(const HANDLE pipe,
                         void* buffer,
                         const DWORD size,
                         const bool write,
                         const Deadline deadline,
                         DWORD& transferred) {
    OVERLAPPED operation{};
    operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (operation.hEvent == nullptr) return false;
    transferred = 0;
    const BOOL started = write
                             ? WriteFile(pipe, buffer, size, &transferred, &operation)
                             : ReadFile(pipe, buffer, size, &transferred, &operation);
    if (!started && GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(operation.hEvent);
        return false;
    }
    if (!started) {
        const DWORD remaining = remaining_milliseconds(deadline);
        const DWORD wait = remaining == 0 ? WAIT_TIMEOUT
                                          : WaitForSingleObject(operation.hEvent, remaining);
        if (wait != WAIT_OBJECT_0) {
            CancelIoEx(pipe, &operation);
            GetOverlappedResult(pipe, &operation, &transferred, TRUE);
            CloseHandle(operation.hEvent);
            SetLastError(wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_OPERATION_ABORTED);
            return false;
        }
        if (!GetOverlappedResult(pipe, &operation, &transferred, FALSE)) {
            CloseHandle(operation.hEvent);
            return false;
        }
    }
    CloseHandle(operation.hEvent);
    return transferred != 0;
}

bool write_all_with_deadline(const HANDLE pipe,
                             const std::string_view bytes,
                             const Deadline deadline) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto chunk = static_cast<DWORD>((std::min)(
            remaining, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD written = 0;
        if (!overlapped_transfer(pipe, const_cast<char*>(bytes.data() + offset),
                                 chunk, true, deadline, written)) return false;
        offset += written;
    }
    return true;
}

bool read_exact_with_deadline(const HANDLE pipe,
                              char* output,
                              const DWORD size,
                              const Deadline deadline) {
    DWORD offset = 0;
    while (offset < size) {
        DWORD read = 0;
        if (!overlapped_transfer(pipe, output + offset, size - offset,
                                 false, deadline, read)) return false;
        offset += read;
    }
    return true;
}

std::string read_frame_with_deadline(const HANDLE pipe, const Deadline deadline) {
    std::array<unsigned char, 4> prefix{};
    if (!read_exact_with_deadline(pipe, reinterpret_cast<char*>(prefix.data()), 4,
                                  deadline)) return {};
    const std::uint32_t size = static_cast<std::uint32_t>(prefix[0]) |
                               (static_cast<std::uint32_t>(prefix[1]) << 8U) |
                               (static_cast<std::uint32_t>(prefix[2]) << 16U) |
                               (static_cast<std::uint32_t>(prefix[3]) << 24U);
    if (size == 0 || size > protocol::kMaximumPayloadBytes) {
        SetLastError(ERROR_INVALID_DATA);
        return {};
    }
    std::string payload(size, '\0');
    if (!read_exact_with_deadline(pipe, payload.data(), size, deadline)) return {};
    return payload;
}

bool write_all(const HANDLE pipe, const std::string_view bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto chunk = static_cast<DWORD>((std::min)(
            remaining, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD written = 0;
        if (!WriteFile(pipe, bytes.data() + offset, chunk, &written, nullptr) || written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

bool read_exact(const HANDLE pipe, char* output, const DWORD size) {
    DWORD offset = 0;
    while (offset < size) {
        DWORD read = 0;
        if (!ReadFile(pipe, output + offset, size - offset, &read, nullptr) || read == 0) {
            return false;
        }
        offset += read;
    }
    return true;
}

bool write_frame(const HANDLE pipe, const std::string_view payload) {
    if (payload.empty() || payload.size() > protocol::kMaximumPayloadBytes) return false;
    return write_all(pipe, protocol::frame(payload));
}

std::string read_frame(const HANDLE pipe) {
    std::array<unsigned char, 4> prefix{};
    if (!read_exact(pipe, reinterpret_cast<char*>(prefix.data()), 4)) return {};
    const std::uint32_t size = static_cast<std::uint32_t>(prefix[0]) |
                               (static_cast<std::uint32_t>(prefix[1]) << 8U) |
                               (static_cast<std::uint32_t>(prefix[2]) << 16U) |
                               (static_cast<std::uint32_t>(prefix[3]) << 24U);
    if (size == 0 || size > protocol::kMaximumPayloadBytes) return {};
    std::string payload(size, '\0');
    if (!read_exact(pipe, payload.data(), size)) return {};
    return payload;
}

}  // namespace

ExchangeResult exchange(const wchar_t* pipe_name,
                        const std::string_view request,
                        const std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (;;) {
        const DWORD remaining = remaining_milliseconds(deadline);
        if (remaining == 0) {
            SetLastError(ERROR_SEM_TIMEOUT);
            return {io_error("WaitNamedPipeW"), {}};
        }
        if (!WaitNamedPipeW(pipe_name, remaining)) {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND) return {io_error("WaitNamedPipeW"), {}};
            Sleep((std::min)(remaining, 2UL));
            continue;
        }
        pipe = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) break;
        const DWORD error = GetLastError();
        if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND)
            return {io_error("CreateFileW"), {}};
        Sleep((std::min)(remaining, 2UL));
    }

    if (request.empty() || request.size() > protocol::kMaximumPayloadBytes ||
        !write_all_with_deadline(pipe, protocol::frame(request), deadline)) {
        const auto status = io_error("WriteFile");
        CloseHandle(pipe);
        return {status, {}};
    }
    auto response = read_frame_with_deadline(pipe, deadline);
    if (response.empty()) {
        const auto status = io_error("ReadFile");
        CloseHandle(pipe);
        return {status, {}};
    }
    CloseHandle(pipe);
    return {{}, std::move(response)};
}

int run_core_server(const wchar_t* pipe_name, const engine::Lexicon& lexicon,
                    engine::UserFrequencyStore* user_frequency,
                    const wchar_t* model_pipe_name,
                    const config::ConfigMonitor* config_monitor,
                    const PluginCandidateProvider* plugin_candidates,
                    const VoiceSessionProvider* voice_sessions) {
    const engine::FullPinyinSchema schema;
    const engine::CandidateGenerator generator(lexicon, nullptr, user_frequency);
    std::size_t unflushed_selections = 0;
    int exit_code = 0;
    bool running = true;
    struct PendingModelRequest {
        std::future<model::ModelMessage> future;
        std::chrono::steady_clock::time_point created;
        std::vector<std::string> candidates;
        std::vector<std::uint64_t> candidate_consumed;
    };
    std::unordered_map<std::string, PendingModelRequest> model_requests;
    const auto model_key = [](const std::uint64_t request_id, const std::uint64_t generation) {
        return std::to_string(request_id) + ':' + std::to_string(generation);
    };
    // LRU cache of full candidate lists, keyed by raw input + correction mode.
    // Repeated typing, paging, and fast-backspace all re-request the same text;
    // re-serving the cached list avoids re-running the chart beam search over the
    // whole lexicon, which stalls on cold dictionary pages.
    constexpr std::size_t kCandidateCacheLimit = 128;
    std::list<std::string> candidate_cache_order;  // front = most recently used
    std::unordered_map<std::string,
        std::pair<std::list<std::string>::iterator, std::vector<engine::Candidate>>>
        candidate_cache;
    // Load the English word list (sibling en_words.txt) for exact-match English
    // fallback. If absent, English recognition is simply disabled. Exact match
    // only -- pinyin inputs are never mistaken for English.
    std::unordered_set<std::string> english_words;
    {
        wchar_t module_path[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, module_path, MAX_PATH) != 0) {
            const auto english_path =
                std::filesystem::path(module_path).parent_path() / L"en_words.txt";
            std::ifstream stream(english_path);
            if (stream) {
                std::string line;
                while (std::getline(stream, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (!line.empty()) english_words.insert(std::move(line));
                }
            }
        }
    }
    while (running) {
        const HANDLE pipe = CreateNamedPipeW(
            pipe_name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, protocol::kMaximumPayloadBytes + 4U, protocol::kMaximumPayloadBytes + 4U,
            5000, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) return 2;

        const BOOL connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (!connected) {
            CloseHandle(pipe);
            continue;
        }

        // Serve every request the client sends on this connection. Tearing the pipe down after
        // a single frame left a window where the next request had to wait for the instance to
        // be recreated, which showed up as visible candidate latency and WaitNamedPipeW
        // timeouts while typing.
        bool client_connected = true;
        while (client_connected && running) {
        const auto request_json = read_frame(pipe);
        if (request_json.empty()) break;
        const auto decoded = protocol::decode_core_request(request_json);
        const auto now = std::chrono::steady_clock::now();
        for (auto pending = model_requests.begin(); pending != model_requests.end();) {
            if (now - pending->second.created > std::chrono::seconds(5) &&
                pending->second.future.wait_for(std::chrono::milliseconds(0)) ==
                    std::future_status::ready)
                pending = model_requests.erase(pending);
            else
                ++pending;
        }
        protocol::Message response{};
        if (!decoded.validation) {
            std::clog << R"({"process":"core_service","module":"ipc","level":"error","event_id":"invalid_request"})"
                      << '\n';
            response.type = protocol::MessageType::error_response;
            response.text = decoded.validation.message;
        } else {
            const auto runtime_config = config_monitor != nullptr
                                            ? config_monitor->snapshot()
                                            : std::shared_ptr<const config::AppConfig>{};
            const bool model_ranking_enabled = model_pipe_name != nullptr &&
                (runtime_config == nullptr || runtime_config->model_ranking_enabled);
            const bool user_learning_enabled = runtime_config == nullptr ||
                runtime_config->user_learning_enabled;
            const auto model_timeout = runtime_config != nullptr
                                           ? runtime_config->model_timeout_ms
                                           : 50U;
            const auto candidate_page_size = runtime_config != nullptr
                                                 ? runtime_config->candidate_page_size
                                                 : 7U;
            response.request_id = decoded.message.request_id;
            response.context_generation = decoded.message.context_generation;
            if (decoded.message.type == protocol::MessageType::shutdown_request) {
                std::clog << R"({"process":"core_service","module":"lifecycle","level":"info","event_id":"shutdown_requested"})"
                          << '\n';
                if (user_frequency != nullptr && unflushed_selections != 0) {
                    const auto flushed = user_frequency->flush();
                    if (!flushed.success) {
                        response.type = protocol::MessageType::error_response;
                        response.text = flushed.error;
                        exit_code = 3;
                    } else {
                        response.type = protocol::MessageType::acknowledgement;
                        response.text = "shutdown_ack";
                    }
                } else {
                    response.type = protocol::MessageType::acknowledgement;
                    response.text = "shutdown_ack";
                }
                running = false;
            } else if (decoded.message.type == protocol::MessageType::voice_start_request ||
                       decoded.message.type == protocol::MessageType::voice_poll_request ||
                       decoded.message.type == protocol::MessageType::voice_cancel_request) {
                response.type = protocol::MessageType::voice_response;
                if (decoded.protocol_version == protocol::kLegacyCoreProtocolVersion) {
                    response.type = protocol::MessageType::error_response;
                    response.text = "voice input requires protocol v7";
                } else if (voice_sessions == nullptr) {
                    response.type = protocol::MessageType::error_response;
                    response.text = "voice broker is unavailable";
                } else {
                    VoiceSessionCommand command = VoiceSessionCommand::poll;
                    std::string_view owner = decoded.message.text;
                    std::string_view language;
                    auto timeout = std::chrono::milliseconds(0);
                    if (decoded.message.type == protocol::MessageType::voice_start_request) {
                        command = VoiceSessionCommand::start;
                        const auto separator = owner.find('\n');
                        if (separator != std::string_view::npos) {
                            language = owner.substr(separator + 1);
                            owner = owner.substr(0, separator);
                        }
                        const auto requested = decoded.message.page;
                        timeout = std::chrono::milliseconds(
                            std::clamp<std::uint64_t>(requested, 1'000, 60'000));
                    } else if (decoded.message.type ==
                               protocol::MessageType::voice_cancel_request) {
                        command = VoiceSessionCommand::cancel;
                    }
                    if (owner.empty() || owner.size() > 128 ||
                        (command == VoiceSessionCommand::start &&
                         (language.empty() || language.size() > 35))) {
                        response.type = protocol::MessageType::error_response;
                        response.text = "invalid voice session request";
                    } else {
                        auto result = (*voice_sessions)(command, owner, language, timeout);
                        response.text = std::move(result.text);
                        response.candidates.emplace_back(voice_state_name(result.state));
                        if (!result.diagnostic.empty())
                            response.candidates.push_back(std::move(result.diagnostic));
                        if (!result.ok && result.state == VoiceSessionState::idle) {
                            response.type = protocol::MessageType::error_response;
                            response.text = response.candidates.size() > 1
                                                ? response.candidates[1]
                                                : "voice broker request failed";
                            response.candidates.clear();
                        }
                    }
                }
            } else if (decoded.message.type == protocol::MessageType::candidate_update_request) {
                response.type = protocol::MessageType::candidate_update_response;
                if (!model_ranking_enabled) {
                    const auto key = model_key(decoded.message.request_id,
                                               decoded.message.context_generation);
                    const auto found = model_requests.find(key);
                    if (found != model_requests.end() &&
                        found->second.future.wait_for(std::chrono::milliseconds(0)) ==
                            std::future_status::ready)
                        model_requests.erase(found);
                } else {
                const auto key = model_key(decoded.message.request_id,
                                           decoded.message.context_generation);
                const auto found = model_requests.find(key);
                if (found != model_requests.end() &&
                    found->second.future.wait_for(std::chrono::milliseconds(0)) !=
                        std::future_status::ready) {
                    response.model_pending = true;
                } else if (found != model_requests.end()) {
                    auto pending = std::move(found->second);
                    model_requests.erase(found);
                    auto update = pending.future.get();
                    if (update.type == model::ModelMessageType::rank_response &&
                        update.status == model::ModelStatus::success) {
                        for (auto& candidate : update.candidates) {
                            const auto original = std::find(
                                pending.candidates.begin(), pending.candidates.end(), candidate);
                            if (original == pending.candidates.end()) continue;
                            const auto index = static_cast<std::size_t>(
                                original - pending.candidates.begin());
                            response.candidates.push_back(std::move(candidate));
                            response.candidate_consumed.push_back(
                                pending.candidate_consumed[index]);
                        }
                    }
                }
                }
            } else if (decoded.message.type == protocol::MessageType::candidate_request) {
                std::clog << R"({"process":"core_service","module":"candidate","level":"info","event_id":"candidate_requested","request_id":)"
                          << decoded.message.request_id << "}\n";
                response.type = protocol::MessageType::candidate_response;
                constexpr std::uint64_t maximum_page = 100;
                constexpr std::size_t maximum_expanded_pages = 8;
                constexpr std::size_t maximum_expanded_candidates = 64;
                if (decoded.message.page > maximum_page ||
                    (decoded.message.expanded && decoded.message.page != 0)) {
                    response.type = protocol::MessageType::error_response;
                    response.text = decoded.message.expanded
                                        ? "expanded candidate request must start at page zero"
                                        : "candidate page exceeds limit";
                } else if (decoded.message.text.starts_with('/')) {
                    response.page = 0;
                    response.expanded = decoded.message.expanded;
                    response.page_size = candidate_page_size;
                    response.correction_enabled = decoded.message.correction_enabled;
                    if (decoded.message.page != 0) {
                        response.has_more = false;
                    } else if (plugin_candidates == nullptr) {
                        response.type = protocol::MessageType::error_response;
                        response.text = "plugin candidate provider is unavailable";
                    } else if (decoded.message.text.size() == 1) {
                        response.type = protocol::MessageType::error_response;
                        response.text = "输入 /smile、/happy、/sad 等表情命令";
                    } else {
                        auto result = (*plugin_candidates)(decoded.message.text.substr(1),
                                                           std::chrono::seconds(3));
                        if (!result.ok) {
                            response.type = protocol::MessageType::error_response;
                            response.text = result.diagnostic.empty()
                                                ? "表情插件调用失败"
                                                : std::move(result.diagnostic);
                        } else {
                            // The slash branch is answered by an out-of-process plugin
                            // (owo_emoji_plugin caps at nine internally). The count is a
                            // UX property of the plugin, not of the pinyin page size, so
                            // we forward everything up to the plugin's own limit instead
                            // of truncating with candidate_page_size.
                            constexpr std::size_t kMaximumPluginCandidates = 9U;
                            const auto count = std::min<std::size_t>(
                                result.candidates.size(), kMaximumPluginCandidates);
                            response.candidates.reserve(count);
                            response.candidate_consumed.reserve(count);
                            for (std::size_t index = 0; index < count; ++index) {
                                if (result.candidates[index].empty()) continue;
                                response.candidates.push_back(
                                    std::move(result.candidates[index]));
                                response.candidate_consumed.push_back(
                                    decoded.message.text.size());
                            }
                        }
                    }
                } else {
                    const auto page = static_cast<std::size_t>(decoded.message.page);
                    const auto page_size = static_cast<std::size_t>(candidate_page_size);
                    const auto expanded_pages = std::min(
                        maximum_expanded_pages, maximum_expanded_candidates / page_size);
                    const auto result_size = decoded.message.expanded
                                                 ? page_size * expanded_pages
                                                 : page_size;
                    const auto begin = decoded.message.expanded ? 0 : page * page_size;
                    const std::size_t needed = begin + result_size + 1;
                    const std::string cache_key = decoded.message.text + '\x1f' +
                                                  (decoded.message.correction_enabled ? '1' : '0');
                    std::vector<engine::Candidate> candidates;
                    auto cache_hit = candidate_cache.find(cache_key);
                    if (cache_hit != candidate_cache.end() &&
                        cache_hit->second.second.size() >= needed) {
                        candidates = cache_hit->second.second;
                        candidate_cache_order.splice(candidate_cache_order.begin(),
                                                     candidate_cache_order, cache_hit->second.first);
                    } else {
                        const auto parsed = schema.parse(decoded.message.text, 32,
                                                         decoded.message.correction_enabled);
                        candidates = generator.generate(parsed, needed);
                        if (cache_hit != candidate_cache.end()) {
                            cache_hit->second.second = candidates;
                            candidate_cache_order.splice(candidate_cache_order.begin(),
                                                         candidate_cache_order, cache_hit->second.first);
                        } else {
                            candidate_cache_order.push_front(cache_key);
                            candidate_cache.emplace(cache_key,
                                std::make_pair(candidate_cache_order.begin(), candidates));
                        }
                        while (candidate_cache.size() > kCandidateCacheLimit) {
                            candidate_cache.erase(candidate_cache_order.back());
                            candidate_cache_order.pop_back();
                        }
                    }
                    const auto end = std::min(candidates.size(), begin + result_size);
                    response.page = decoded.message.expanded ? 0 : decoded.message.page;
                    response.expanded = decoded.message.expanded;
                    response.page_size = candidate_page_size;
                    response.correction_enabled = decoded.message.correction_enabled;
                    response.has_more = candidates.size() > end;
                    if (!candidates.empty())
                        response.syllables = candidates.front().source_segments;
                    if (begin < candidates.size()) {
                        response.candidates.reserve(end - begin);
                        response.candidate_consumed.reserve(end - begin);
                        for (std::size_t index = begin; index < end; ++index) {
                            response.candidates.push_back(candidates[index].text);
                            response.candidate_consumed.push_back(
                                candidates[index].consumed_input_bytes);
                        }
                    }
                    // English exact-match: if the raw input is a known English
                    // word, surface it as the top candidate. Exact match only;
                    // response.syllables is NOT touched so the TSF pinyin preview
                    // keeps the user's original input.
                    if (!english_words.empty() &&
                        english_words.contains(decoded.message.text)) {
                        response.candidates.insert(response.candidates.begin(),
                                                   decoded.message.text);
                        response.candidate_consumed.insert(
                            response.candidate_consumed.begin(),
                            decoded.message.text.size());
                    }
                }
                // Transitional compatibility for the P1 TSF consumer. It is removed when
                // TSF owns a paged candidate list later in P2.1.
                if (!response.candidates.empty()) response.text = response.candidates.front();
                if (model_ranking_enabled && !decoded.message.expanded &&
                    !response.candidates.empty() &&
                    model_requests.size() < 128) {
                    model::ModelMessage model_request;
                    model_request.type = model::ModelMessageType::rank_request;
                    model_request.status = model::ModelStatus::success;
                    model_request.request_id = decoded.message.request_id;
                    model_request.timeout_ms = model_timeout;
                    model_request.model_id = "owo.mock.rank.v1";
                    model_request.input = decoded.message.text;
                    model_request.candidates = response.candidates;
                    const auto original_candidates = response.candidates;
                    const auto original_consumed = response.candidate_consumed;
                    const std::wstring model_pipe(model_pipe_name);
                    model_requests.insert_or_assign(
                        model_key(decoded.message.request_id,
                                  decoded.message.context_generation),
                        PendingModelRequest{std::async(std::launch::async,
                                   [model_pipe, request = std::move(model_request), model_timeout] {
                            const auto exchanged = exchange(
                                model_pipe.c_str(), model::encode_model_message(request),
                                std::chrono::milliseconds(model_timeout + 25U));
                            if (!exchanged.status) return model::ModelMessage{};
                            const auto decoded_model =
                                model::decode_model_message(exchanged.response);
                            return decoded_model.validation ? decoded_model.message
                                                            : model::ModelMessage{};
                        }), std::chrono::steady_clock::now(), original_candidates,
                            original_consumed});
                    response.model_pending = true;
                }
            } else if (decoded.message.type == protocol::MessageType::candidate_committed) {
                response.type = protocol::MessageType::acknowledgement;
                response.text = "commit_ack";
                if (user_frequency != nullptr && user_learning_enabled &&
                    !decoded.message.text.empty()) {
                    user_frequency->record(decoded.message.text);
                    ++unflushed_selections;
                }
                if (unflushed_selections >= 32) {
                    const auto flushed = user_frequency->flush();
                    if (flushed.success) unflushed_selections = 0;
                    else {
                        response.type = protocol::MessageType::error_response;
                        response.text = flushed.error;
                    }
                }
            } else {
                response.type = protocol::MessageType::error_response;
                response.text = "unsupported request type";
            }
        }

        auto encoded = protocol::encode_core_response(response, decoded.protocol_version);
        if (encoded.empty() && decoded.protocol_version == protocol::kLegacyCoreProtocolVersion) {
            protocol::Message legacy_error;
            legacy_error.type = protocol::MessageType::error_response;
            legacy_error.request_id = response.request_id;
            legacy_error.context_generation = response.context_generation;
            legacy_error.text = "legacy protocol response exceeds limits";
            encoded = protocol::encode_core_response(
                legacy_error, protocol::kLegacyCoreProtocolVersion);
        }
        if (!encoded.empty() && !write_frame(pipe, encoded)) client_connected = false;
        FlushFileBuffers(pipe);
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    return exit_code;
}

int run_core_server(const wchar_t* pipe_name, const engine::Lexicon& lexicon,
                    engine::UserFrequencyStore* user_frequency,
                    const wchar_t* model_pipe_name) {
    return run_core_server(pipe_name, lexicon, user_frequency, model_pipe_name, nullptr);
}

int run_core_server(const wchar_t* pipe_name, const engine::Lexicon& lexicon,
                    engine::UserFrequencyStore* user_frequency) {
    return run_core_server(pipe_name, lexicon, user_frequency, nullptr);
}

int run_core_server(const wchar_t* pipe_name, const engine::Lexicon& lexicon) {
    return run_core_server(pipe_name, lexicon, nullptr);
}

int run_core_server(const wchar_t* pipe_name) {
    const engine::MemoryLexicon fallback({
        {{"ni", "hao"}, "你好", 1000},
        {{"ni", "hao"}, "你号", 50},
        {{"xian"}, "先", 800},
        {{"xian"}, "线", 700},
        {{"xi", "an"}, "西安", 900},
    });
    return run_core_server(pipe_name, fallback);
}

int run_model_server(const wchar_t* pipe_name, model::IModelBackend& backend) {
    bool running = true;
    while (running) {
        const HANDLE pipe = CreateNamedPipeW(
            pipe_name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, protocol::kMaximumPayloadBytes + 4U, protocol::kMaximumPayloadBytes + 4U,
            5000, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) return 2;
        const BOOL connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (!connected) {
            CloseHandle(pipe);
            continue;
        }

        const auto decoded = model::decode_model_message(read_frame(pipe));
        model::ModelMessage response;
        if (!decoded.validation) {
            response.type = model::ModelMessageType::error_response;
            response.status = model::ModelStatus::backend_error;
            response.diagnostic = decoded.validation.message;
        } else {
            response.request_id = decoded.message.request_id;
            if (decoded.message.type == model::ModelMessageType::shutdown_request) {
                response.type = model::ModelMessageType::acknowledgement;
                response.status = model::ModelStatus::success;
                running = false;
            } else if (decoded.message.type == model::ModelMessageType::rank_request) {
                if (decoded.message.model_id != backend.id()) {
                    response.type = model::ModelMessageType::error_response;
                    response.status = model::ModelStatus::backend_error;
                    response.diagnostic = "model backend is unavailable";
                } else {
                    model::ModelRequest request;
                    request.request_id = decoded.message.request_id;
                    request.model_id = decoded.message.model_id;
                    request.input = decoded.message.input;
                    request.candidates = decoded.message.candidates;
                    request.timeout = std::chrono::milliseconds(decoded.message.timeout_ms);
                    auto result = backend.rank(request, {});
                    response.type = model::ModelMessageType::rank_response;
                    response.status = result.status;
                    response.candidates = std::move(result.candidates);
                    response.diagnostic = std::move(result.diagnostic);
                }
            } else {
                response.type = model::ModelMessageType::error_response;
                response.status = model::ModelStatus::backend_error;
                response.diagnostic = "unsupported model message type";
            }
        }
        const auto encoded = model::encode_model_message(response);
        if (!encoded.empty()) write_frame(pipe, encoded);
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    return 0;
}

}  // namespace owo::ipc
