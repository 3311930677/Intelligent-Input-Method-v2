#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

namespace {

bool launch(const std::wstring& command, PROCESS_INFORMATION& process) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    std::wstring mutable_command = command;
    if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process)) return false;
    CloseHandle(process.hThread);
    process.hThread = nullptr;
    return true;
}

DWORD wait_and_close(PROCESS_INFORMATION& process, const DWORD timeout = 5000) {
    if (WaitForSingleObject(process.hProcess, timeout) != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 99);
        WaitForSingleObject(process.hProcess, 1000);
    }
    DWORD exit_code = STILL_ACTIVE;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    process.hProcess = nullptr;
    return exit_code;
}

std::wstring quote(const std::wstring_view value) { return L"\"" + std::wstring(value) + L"\""; }

}  // namespace

int wmain(const int argc, wchar_t** argv) {
    if (argc != 3) return 1;
    const std::filesystem::path root(argv[2]);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    if (error) return 2;
    const auto path = root / L"owo.conf";
    const std::wstring executable = quote(argv[1]);
    const std::wstring config = quote(path.wstring());

    PROCESS_INFORMATION initial{};
    if (!launch(executable + L" " + config + L" set candidate_page_size 5", initial) ||
        wait_and_close(initial) != 0) return 3;

    PROCESS_INFORMATION watcher{};
    if (!launch(executable + L" " + config + L" watch 3000", watcher)) return 4;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    PROCESS_INFORMATION setter{};
    if (!launch(executable + L" " + config + L" set candidate_page_size 7", setter) ||
        wait_and_close(setter) != 0 || wait_and_close(watcher) != 0) return 5;

    std::ifstream input(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    input.close();
    if (bytes.find("candidate_page_size=7\n") == std::string::npos) return 6;
    PROCESS_INFORMATION invalid{};
    if (!launch(executable + L" " + config + L" set candidate_page_size 99", invalid)) return 7;
    if (wait_and_close(invalid) == 0) return 8;
    std::ifstream unchanged_input(path, std::ios::binary);
    const std::string unchanged((std::istreambuf_iterator<char>(unchanged_input)), {});
    unchanged_input.close();
    if (unchanged != bytes) return 9;

    PROCESS_INFORMATION show{};
    if (!launch(executable + L" " + config + L" show", show) || wait_and_close(show) != 0) return 10;
    { std::ofstream corrupt(path, std::ios::binary | std::ios::trunc); corrupt << "broken"; }
    PROCESS_INFORMATION repair{};
    if (!launch(executable + L" " + config + L" repair", repair) ||
        wait_and_close(repair) != 0) return 11;
    std::ifstream repaired_input(path, std::ios::binary);
    const std::string repaired((std::istreambuf_iterator<char>(repaired_input)), {});
    if (repaired.find("candidate_page_size=5\n") == std::string::npos) return 12;
    std::filesystem::remove_all(root, error);
    return 0;
}
