#include "owo/ipc/named_pipe.h"

#include "owo/protocol/messages.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <limits>

namespace owo::ipc {
namespace {

protocol::ValidationResult io_error(const char* operation) {
    return {protocol::ErrorCode::invalid_payload,
            std::string(operation) + " failed with Win32 error " +
                std::to_string(GetLastError())};
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
    if (!WaitNamedPipeW(pipe_name, static_cast<DWORD>(timeout.count()))) {
        return {io_error("WaitNamedPipeW"), {}};
    }
    const HANDLE pipe = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return {io_error("CreateFileW"), {}};

    if (!write_frame(pipe, request)) {
        const auto status = io_error("WriteFile");
        CloseHandle(pipe);
        return {status, {}};
    }
    auto response = read_frame(pipe);
    if (response.empty()) {
        const auto status = io_error("ReadFile");
        CloseHandle(pipe);
        return {status, {}};
    }
    CloseHandle(pipe);
    return {{}, std::move(response)};
}

int run_core_server(const wchar_t* pipe_name) {
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

        const auto request_json = read_frame(pipe);
        const auto decoded = protocol::decode_message(request_json);
        protocol::Message response{};
        if (!decoded.validation) {
            response.type = protocol::MessageType::error_response;
            response.text = decoded.validation.message;
        } else {
            response.request_id = decoded.message.request_id;
            response.context_generation = decoded.message.context_generation;
            if (decoded.message.type == protocol::MessageType::shutdown_request) {
                response.type = protocol::MessageType::candidate_response;
                response.text = "shutdown_ack";
                running = false;
            } else if (decoded.message.type == protocol::MessageType::candidate_request) {
                response.type = protocol::MessageType::candidate_response;
                response.text = "固定候选";
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
    return 0;
}

}  // namespace owo::ipc

