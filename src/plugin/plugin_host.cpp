#include "owo/plugin/plugin_host.h"

#include "owo/plugin/plugin_pipe.h"
#include "owo/plugin/plugin_sandbox.h"
#include "owo/plugin/plugin_store.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <userenv.h>
#endif

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace owo::plugin {
namespace {

PluginHostLaunchResult launch_failure(std::string diagnostic) {
    return {{}, {}, {}, {}, std::move(diagnostic)};
}

#ifdef _WIN32
using Deadline = std::chrono::steady_clock::time_point;

std::string win32_error(const char* operation, const DWORD error = GetLastError()) {
    return std::string(operation) + " failed with Win32 error " + std::to_string(error);
}

std::chrono::milliseconds remaining(const Deadline deadline) {
    const auto value = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    return value.count() > 0 ? value : std::chrono::milliseconds(0);
}

std::wstring current_user_sid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> buffer(size);
    if (size == 0 || !GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        CloseHandle(token);
        return {};
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    LPWSTR text = nullptr;
    const bool converted = ConvertSidToStringSidW(user->User.Sid, &text) != FALSE;
    std::wstring result = converted && text != nullptr ? text : L"";
    if (text != nullptr) LocalFree(text);
    CloseHandle(token);
    return result;
}

bool collect_safe_tree(const std::filesystem::path& root,
                       std::vector<std::filesystem::path>& paths,
                       std::string& diagnostic) {
    const auto root_attributes = GetFileAttributesW(root.c_str());
    if (root_attributes == INVALID_FILE_ATTRIBUTES ||
        (root_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (root_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        diagnostic = "plugin runtime directory is missing or unsafe";
        return false;
    }
    paths = {root};
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
         iterator != end && !error; iterator.increment(error)) {
        if (paths.size() >= 2048) {
            diagnostic = "plugin runtime directory contains too many entries";
            return false;
        }
        const auto attributes = GetFileAttributesW(iterator->path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            diagnostic = "plugin runtime directory contains an unsafe entry";
            return false;
        }
        paths.push_back(iterator->path());
    }
    if (error) {
        diagnostic = "cannot enumerate plugin runtime directory";
        return false;
    }
    return true;
}

bool apply_tree_dacl(const std::filesystem::path& root,
                     const std::wstring& sddl,
                     std::string& diagnostic) {
    std::vector<std::filesystem::path> paths;
    if (!collect_safe_tree(root, paths, diagnostic)) return false;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
        diagnostic = win32_error("ConvertStringSecurityDescriptor");
        return false;
    }
    BOOL present = FALSE;
    BOOL defaulted = FALSE;
    PACL dacl = nullptr;
    if (!GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) ||
        !present || dacl == nullptr) {
        LocalFree(descriptor);
        diagnostic = "cannot obtain plugin runtime DACL";
        return false;
    }
    for (const auto& path : paths) {
        const auto status = SetNamedSecurityInfoW(
            const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            nullptr, nullptr, dacl, nullptr);
        if (status != ERROR_SUCCESS) {
            LocalFree(descriptor);
            diagnostic = win32_error("SetNamedSecurityInfoW", status);
            return false;
        }
    }
    LocalFree(descriptor);
    return true;
}

bool prepare_runtime_access(const std::filesystem::path& installed,
                            const std::filesystem::path& data,
                            const std::wstring& appcontainer_sid,
                            std::string& diagnostic) {
    const auto user_sid = current_user_sid();
    if (user_sid.empty()) {
        diagnostic = "cannot resolve current user SID for plugin runtime ACL";
        return false;
    }
    const std::wstring administrators = L"(A;OICI;FA;;;BA)";
    const std::wstring system = L"(A;OICI;FA;;;SY)";
    const std::wstring owner = L"(A;OICI;FA;;;" + user_sid + L")";
    const std::wstring installed_sddl = L"D:P" + administrators + system + owner +
        L"(A;OICI;GRGX;;;" + appcontainer_sid + L")";
    const std::wstring data_sddl = L"D:P" + administrators + system + owner +
        L"(A;OICI;0x1301bf;;;" + appcontainer_sid + L")";
    return apply_tree_dacl(installed, installed_sddl, diagnostic) &&
           apply_tree_dacl(data, data_sddl, diagnostic);
}

bool safe_entry(const std::filesystem::path& installed,
                const std::string_view relative_entry,
                std::filesystem::path& entry) {
    entry = (installed / std::filesystem::path(relative_entry)).lexically_normal();
    const auto expected_parent = installed.lexically_normal();
    const auto relative = entry.lexically_relative(expected_parent);
    if (relative.empty() || *relative.begin() == L"..") return false;
    const auto attributes = GetFileAttributesW(entry.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
           entry.extension() == L".exe";
}

std::wstring quote_argument(const std::wstring_view argument) {
    std::wstring result(1, L'"');
    std::size_t backslashes = 0;
    for (const auto character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'"');
        } else {
            result.append(backslashes, L'\\');
            result.push_back(character);
        }
        backslashes = 0;
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

std::wstring ascii_wide(const std::string_view value) {
    std::wstring result;
    result.reserve(value.size());
    for (const unsigned char byte : value) result.push_back(static_cast<wchar_t>(byte));
    return result;
}

std::wstring environment_value(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) return {};
    std::wstring value(required, L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, value.data(), required);
    if (copied == 0 || copied >= required) return {};
    value.resize(copied);
    return value;
}

std::vector<wchar_t> restricted_environment(const std::filesystem::path& data_path) {
    wchar_t windows_directory[MAX_PATH]{};
    const auto length = GetWindowsDirectoryW(windows_directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};
    std::vector<std::wstring> variables;
    // CreateProcess needs a small set of Windows/user-profile coordinates when applying
    // SECURITY_CAPABILITIES. Keep an explicit allowlist so arbitrary parent secrets never
    // cross the sandbox boundary.
    constexpr const wchar_t* allowlist[]{
        L"ALLUSERSPROFILE", L"APPDATA", L"CommonProgramFiles",
        L"CommonProgramFiles(x86)", L"CommonProgramW6432", L"DriverData",
        L"LOCALAPPDATA", L"NUMBER_OF_PROCESSORS", L"OS",
        L"PROCESSOR_ARCHITECTURE", L"PROCESSOR_IDENTIFIER", L"PROCESSOR_LEVEL",
        L"PROCESSOR_REVISION", L"ProgramData", L"ProgramFiles", L"ProgramFiles(x86)",
        L"ProgramW6432", L"PUBLIC", L"SystemDrive", L"USERPROFILE",
    };
    for (const auto* name : allowlist) {
        auto value = environment_value(name);
        if (!value.empty()) variables.emplace_back(std::wstring(name) + L"=" + value);
    }
    variables.insert(variables.end(), {
        L"OWO_PLUGIN_DATA=" + data_path.native(),
        L"SystemRoot=" + std::wstring(windows_directory, length),
        L"TEMP=" + data_path.native(),
        L"TMP=" + data_path.native(),
        L"WINDIR=" + std::wstring(windows_directory, length),
    });
    std::sort(variables.begin(), variables.end(), [](const auto& left, const auto& right) {
        return _wcsicmp(left.c_str(), right.c_str()) < 0;
    });
    std::size_t total = 1;
    for (const auto& variable : variables) total += variable.size() + 1;
    std::vector<wchar_t> block;
    block.reserve(total);
    for (const auto& variable : variables) {
        block.insert(block.end(), variable.begin(), variable.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}
#endif

}  // namespace

struct PluginHostSession::Impl {
    PluginPipe pipe;
    std::string plugin_id;
    std::string version;
#ifdef _WIN32
    HANDLE process{};
    HANDLE job{};
    DWORD process_id{};
#endif
};

PluginHostSession::PluginHostSession() = default;
PluginHostSession::PluginHostSession(std::unique_ptr<Impl> implementation)
    : implementation_(std::move(implementation)) {}
PluginHostSession::PluginHostSession(PluginHostSession&& other) noexcept
    : implementation_(std::move(other.implementation_)) {}
PluginHostSession& PluginHostSession::operator=(PluginHostSession&& other) noexcept {
    if (this != &other) {
        terminate();
        implementation_ = std::move(other.implementation_);
    }
    return *this;
}
PluginHostSession::~PluginHostSession() { terminate(); }

bool PluginHostSession::valid() const noexcept {
#ifdef _WIN32
    return implementation_ != nullptr && implementation_->process != nullptr &&
           implementation_->job != nullptr && implementation_->pipe.valid();
#else
    return false;
#endif
}

std::string_view PluginHostSession::plugin_id() const noexcept {
    return implementation_ != nullptr ? implementation_->plugin_id : std::string_view{};
}

std::string_view PluginHostSession::version() const noexcept {
    return implementation_ != nullptr ? implementation_->version : std::string_view{};
}

unsigned long PluginHostSession::process_id() const noexcept {
#ifdef _WIN32
    return implementation_ != nullptr ? implementation_->process_id : 0;
#else
    return 0;
#endif
}

void PluginHostSession::terminate() noexcept {
#ifdef _WIN32
    if (implementation_ == nullptr) return;
    if (implementation_->job != nullptr) {
        TerminateJobObject(implementation_->job, ERROR_PROCESS_ABORTED);
        if (implementation_->process != nullptr)
            WaitForSingleObject(implementation_->process, 2000);
    }
    implementation_->pipe = {};
    if (implementation_->process != nullptr) CloseHandle(implementation_->process);
    if (implementation_->job != nullptr) CloseHandle(implementation_->job);
    implementation_->process = nullptr;
    implementation_->job = nullptr;
    implementation_->process_id = 0;
#endif
}

PluginHostOperationResult PluginHostSession::shutdown(
    const std::chrono::milliseconds timeout) {
#ifdef _WIN32
    if (!valid() || timeout.count() <= 0)
        return {false, false, "invalid PluginHost shutdown request"};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    PluginMessage request;
    request.type = PluginMessageType::shutdown_request;
    request.status = PluginStatus::success;
    request.request_id = 2;
    request.plugin_id = implementation_->plugin_id;
    auto remaining_time = remaining(deadline);
    auto sent = send_plugin_pipe_message(implementation_->pipe, request, remaining_time);
    if (!sent.ok) {
        const auto diagnostic = "plugin shutdown send failed: " + sent.diagnostic;
        terminate();
        return {false, true, diagnostic};
    }
    remaining_time = remaining(deadline);
    auto response = receive_plugin_pipe_message(implementation_->pipe, remaining_time);
    if (!response.status.ok ||
        response.message.type != PluginMessageType::acknowledgement ||
        response.message.request_id != request.request_id ||
        response.message.plugin_id != implementation_->plugin_id) {
        const auto diagnostic = response.status.ok
            ? std::string("plugin returned an invalid shutdown acknowledgement")
            : "plugin shutdown acknowledgement failed: " + response.status.diagnostic;
        terminate();
        return {false, true, diagnostic};
    }
    remaining_time = remaining(deadline);
    const DWORD waited = remaining_time.count() <= 0 ? WAIT_TIMEOUT :
        WaitForSingleObject(implementation_->process,
            static_cast<DWORD>((std::min)(remaining_time.count(),
                                         static_cast<long long>(INFINITE - 1))));
    DWORD exit_code = ERROR_PROCESS_ABORTED;
    const bool exited = waited == WAIT_OBJECT_0 &&
        GetExitCodeProcess(implementation_->process, &exit_code) != FALSE;
    if (!exited || exit_code != 0) {
        const auto diagnostic = !exited ? "plugin did not exit within its shutdown deadline"
                                        : "plugin exited with a nonzero status";
        terminate();
        return {false, true, diagnostic};
    }
    implementation_->pipe = {};
    CloseHandle(implementation_->process);
    CloseHandle(implementation_->job);
    implementation_->process = nullptr;
    implementation_->job = nullptr;
    implementation_->process_id = 0;
    return {true, false, {}};
#else
    static_cast<void>(timeout);
    return {false, false, "PluginHost lifecycle is currently available on Windows only"};
#endif
}

PluginHostLaunchResult launch_active_plugin(
    const std::filesystem::path& plugin_store_root,
    const std::string_view plugin_id,
    const std::chrono::milliseconds startup_timeout) {
#ifdef _WIN32
    if (startup_timeout.count() <= 0)
        return launch_failure("invalid PluginHost startup timeout");
    const auto installed = query_active_plugin_version(plugin_store_root, plugin_id);
    if (!installed.ok) return launch_failure(installed.diagnostic);
    if (installed.manifest.runtime != "process" || installed.manifest.network)
        return launch_failure("active plugin runtime policy is unsupported");
    const auto profile = prepare_plugin_sandbox_profile(installed.manifest.id);
    if (!profile.ok) return launch_failure(profile.diagnostic);
    const auto data_path = plugin_store_root.lexically_normal() / L"data" /
        std::filesystem::path(installed.manifest.id);
    std::string access_diagnostic;
    if (!prepare_runtime_access(installed.installed_path, data_path,
                                profile.sid_string, access_diagnostic))
        return launch_failure(access_diagnostic);
    std::filesystem::path entry;
    if (!safe_entry(installed.installed_path, installed.manifest.entry, entry))
        return launch_failure("active plugin entry point is missing or unsafe");
    auto pipe = create_plugin_pipe_server(profile.sid_string);
    if (!pipe) return launch_failure(pipe.diagnostic);
    PSID appcontainer_sid = nullptr;
    if (FAILED(DeriveAppContainerSidFromAppContainerName(
            profile.profile_name.c_str(), &appcontainer_sid)) || appcontainer_sid == nullptr)
        return launch_failure("cannot derive active plugin AppContainer SID");
    SIZE_T attribute_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
    auto* attributes = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
        HeapAlloc(GetProcessHeap(), 0, attribute_size));
    SECURITY_CAPABILITIES capabilities{};
    capabilities.AppContainerSid = appcontainer_sid;
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    const bool attributes_ready = attributes != nullptr &&
        InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size) != FALSE &&
        UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                  &capabilities, sizeof(capabilities), nullptr, nullptr) != FALSE;
    startup.lpAttributeList = attributes;
    if (!attributes_ready) {
        if (attributes != nullptr) HeapFree(GetProcessHeap(), 0, attributes);
        FreeSid(appcontainer_sid);
        return launch_failure(win32_error("cannot prepare plugin process attributes"));
    }
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
        JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_PROCESS_MEMORY |
        JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    limits.ProcessMemoryLimit = 128ULL * 1024ULL * 1024ULL;
    if (job == nullptr || !SetInformationJobObject(
            job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        const DWORD error = GetLastError();
        if (job != nullptr) CloseHandle(job);
        DeleteProcThreadAttributeList(attributes);
        HeapFree(GetProcessHeap(), 0, attributes);
        FreeSid(appcontainer_sid);
        return launch_failure(win32_error("cannot prepare plugin Job", error));
    }
    const auto wide_id = ascii_wide(installed.manifest.id);
    auto environment = restricted_environment(data_path);
    if (environment.empty()) {
        CloseHandle(job);
        DeleteProcThreadAttributeList(attributes);
        HeapFree(GetProcessHeap(), 0, attributes);
        FreeSid(appcontainer_sid);
        return launch_failure("cannot prepare restricted plugin environment");
    }
    std::wstring command = quote_argument(entry.native()) + L" --owo-plugin-pipe " +
        quote_argument(pipe.pipe_name) + L" --owo-plugin-id " + quote_argument(wide_id) +
        L" --owo-plugin-data " + quote_argument(data_path.native());
    PROCESS_INFORMATION process{};
    const DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
        CREATE_DEFAULT_ERROR_MODE | EXTENDED_STARTUPINFO_PRESENT;
    const bool launched = CreateProcessW(
        entry.c_str(), command.data(), nullptr, nullptr, FALSE, flags,
        environment.data(), installed.installed_path.c_str(),
        &startup.StartupInfo, &process) != FALSE;
    const DWORD launch_error = launched ? ERROR_SUCCESS : GetLastError();
    const bool assigned = launched &&
        AssignProcessToJobObject(job, process.hProcess) != FALSE;
    const DWORD assign_error = launched && !assigned ? GetLastError() : ERROR_SUCCESS;
    const bool resumed = assigned &&
        ResumeThread(process.hThread) != static_cast<DWORD>(-1);
    const DWORD resume_error = assigned && !resumed ? GetLastError() : ERROR_SUCCESS;
    if (launched) CloseHandle(process.hThread);
    DeleteProcThreadAttributeList(attributes);
    HeapFree(GetProcessHeap(), 0, attributes);
    FreeSid(appcontainer_sid);
    if (!launched || !assigned || !resumed) {
        if (launched) TerminateProcess(process.hProcess, ERROR_PROCESS_ABORTED);
        if (launched) CloseHandle(process.hProcess);
        CloseHandle(job);
        return launch_failure(!launched ? win32_error("CreateProcessW", launch_error)
            : !assigned ? win32_error("AssignProcessToJobObject", assign_error)
                        : win32_error("ResumeThread", resume_error));
    }
    auto implementation = std::make_unique<PluginHostSession::Impl>();
    implementation->pipe = std::move(pipe.pipe);
    implementation->plugin_id = installed.manifest.id;
    implementation->version = installed.manifest.version;
    implementation->process = process.hProcess;
    implementation->job = job;
    implementation->process_id = process.dwProcessId;
    PluginHostSession session(std::move(implementation));
    const auto deadline = std::chrono::steady_clock::now() + startup_timeout;
    auto accepted = accept_plugin_pipe_client(session.implementation_->pipe,
                                              remaining(deadline));
    if (!accepted.ok) {
        session.terminate();
        return launch_failure("plugin pipe accept failed: " + accepted.diagnostic);
    }
    auto hello = receive_plugin_pipe_message(session.implementation_->pipe,
                                             remaining(deadline));
    if (!hello.status.ok || hello.message.type != PluginMessageType::hello_request ||
        hello.message.plugin_id != installed.manifest.id) {
        const auto diagnostic = hello.status.ok ? "plugin returned an invalid hello request"
            : "plugin hello failed: " + hello.status.diagnostic;
        session.terminate();
        return launch_failure(diagnostic);
    }
    PluginMessage response;
    response.type = PluginMessageType::hello_response;
    response.status = PluginStatus::success;
    response.request_id = hello.message.request_id;
    response.plugin_id = installed.manifest.id;
    auto sent = send_plugin_pipe_message(session.implementation_->pipe, response,
                                         remaining(deadline));
    if (!sent.ok) {
        session.terminate();
        return launch_failure("plugin hello response failed: " + sent.diagnostic);
    }
    return {std::move(session), installed.manifest, installed.installed_path, data_path, {}};
#else
    static_cast<void>(plugin_store_root); static_cast<void>(plugin_id);
    static_cast<void>(startup_timeout);
    return launch_failure("PluginHost lifecycle is currently available on Windows only");
#endif
}

}  // namespace owo::plugin
