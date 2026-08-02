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
#include <limits>
#include <chrono>
#include <future>
#include <iostream>
#include <unordered_map>
#include <utility>

namespace owo::ipc {
namespace {

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
                    const config::ConfigMonitor* config_monitor) {
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

        const auto request_json = read_frame(pipe);
        const auto decoded = protocol::decode_message(request_json);
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
                                                 : 5U;
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
                if (decoded.message.page > maximum_page) {
                    response.type = protocol::MessageType::error_response;
                    response.text = "candidate page exceeds limit";
                } else {
                    const auto page = static_cast<std::size_t>(decoded.message.page);
                    const auto page_size = static_cast<std::size_t>(candidate_page_size);
                    const auto begin = page * page_size;
                    const auto parsed = schema.parse(decoded.message.text);
                    const auto candidates = generator.generate(parsed, begin + page_size + 1);
                    const auto end = std::min(candidates.size(), begin + page_size);
                    response.page = decoded.message.page;
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
                }
                // Transitional compatibility for the P1 TSF consumer. It is removed when
                // TSF owns a paged candidate list later in P2.1.
                if (!response.candidates.empty()) response.text = response.candidates.front();
                if (model_ranking_enabled && !response.candidates.empty() &&
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

        const auto encoded = protocol::encode_message(response);
        if (!encoded.empty()) write_frame(pipe, encoded);
        FlushFileBuffers(pipe);
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
