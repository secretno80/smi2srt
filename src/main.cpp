#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include "resource.h"

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// subConverter Explorer front end.
//
// This executable only gathers the files Explorer hands it (one process per
// selected file) into a single batch, runs the conversion engine
// (subConverterEngine\subConverterEngine.exe, Python + pysubs2/pycaption)
// once for the whole batch, and shows the summary. All parsing, conversion,
// naming and matching live in the engine.
// ─────────────────────────────────────────────────────────────────────────────

std::wstring Trim(const std::wstring& value) {
    size_t begin = 0;
    while (begin < value.size() && std::iswspace(value[begin])) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::iswspace(value[end - 1])) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::wstring ToLowerW(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool IsSubtitleExtension(const std::wstring& extension) {
    std::wstring ext = ToLowerW(extension);
    return ext == L".smi" || ext == L".srt" || ext == L".ass";
}

// Returns the engine's --to value ("smi"/"srt"/"ass"), or empty if |arg|
// isn't a target-format switch.
std::wstring ParseTargetFormat(const std::wstring& arg) {
    std::wstring lower = ToLowerW(arg);
    for (const wchar_t* fmt : {L"smi", L"srt", L"ass"}) {
        std::wstring f(fmt);
        if (lower == L"/to:" + f || lower == L"--to=" + f || lower == L"-to:" + f) {
            return f;
        }
    }
    return L"";
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }
    int needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(std::max(needed, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }
    int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(std::max(needed, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), needed);
    return out;
}

void DebugLog(const std::wstring& message) {
    const char* env = std::getenv("SUBCONVERTER_DEBUG");
    if (!env || env[0] == '\0') {
        env = std::getenv("SMI2SRT_DEBUG");
    }
    if (!env || env[0] == '\0') {
        return;
    }

    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring path = tmp;
    if (!path.empty() && path.back() != L'\\') {
        path += L'\\';
    }
    path += L"subConverter_debug.log";

    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t prefix[64] = {};
    swprintf_s(prefix, L"[%04d-%02d-%02d %02d:%02d:%02d] ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    std::wstring line = std::wstring(prefix) + message + L"\r\n";
    DWORD wrote = 0;
    WriteFile(h, line.data(), static_cast<DWORD>(line.size() * sizeof(wchar_t)), &wrote, nullptr);
    CloseHandle(h);
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-instance file queue
// Explorer launches one process per file when registered with %1.
// The first process becomes the "collector": it waits while the other
// instances each enqueue their file path into a shared temp directory, then
// exit immediately.  The collector drains the queue and processes every file
// in one batch, showing a single summary notification.
//
// For large multi-selects (10+ files) Explorer does not necessarily spawn all
// the per-file processes at once — they can arrive in separate bursts a few
// hundred ms apart. A fixed sleep after the first arrival can elapse before
// the stragglers enqueue, so the collector drains early and the selection
// gets split into multiple output batches (e.g. 24 files becoming 12+9+3).
// Instead of a fixed delay, poll the queue file and keep waiting as long as
// it keeps growing, only draining once it has been quiet for
// COLLECT_QUIET_MS — capped at COLLECT_MAX_MS so a wedged instance can't hang
// the collector indefinitely.
// ─────────────────────────────────────────────────────────────────────────────

static constexpr DWORD COLLECT_POLL_MS = 50;
static constexpr DWORD COLLECT_QUIET_MS = 300;
static constexpr DWORD COLLECT_MAX_MS = 4000;
static const wchar_t* COLLECTOR_MUTEX_NAME = L"Local\\smi2srt_collector";
static const wchar_t* QUEUE_WRITE_MUTEX_NAME = L"Local\\smi2srt_queue_writer";
static const wchar_t* QUEUE_FILE_NAME = L"smi2srt_queue.txt";

std::wstring GetTempFilePath(const std::wstring& fileName) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring path = tmp;
    if (!path.empty() && path.back() != L'\\') {
        path += L'\\';
    }
    path += fileName;
    return path;
}

void EnqueueFiles(const std::vector<fs::path>& files, const wchar_t* writeMutexName, const wchar_t* queueFileName) {
    HANDLE writeMutex = CreateMutexW(nullptr, FALSE, writeMutexName);
    if (!writeMutex) {
        return;
    }

    WaitForSingleObject(writeMutex, INFINITE);

    std::wstring queuePath = GetTempFilePath(queueFileName);
    HANDLE h = CreateFileW(queuePath.c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        ReleaseMutex(writeMutex);
        CloseHandle(writeMutex);
        return;
    }

    for (const auto& p : files) {
        std::wstring line = p.wstring() + L"\n";
        DWORD wrote = 0;
        WriteFile(h,
                  line.data(),
                  static_cast<DWORD>(line.size() * sizeof(wchar_t)),
                  &wrote,
                  nullptr);
    }
    CloseHandle(h);

    ReleaseMutex(writeMutex);
    CloseHandle(writeMutex);
}

// Blocks the collector until the queue file stops growing (no straggler
// process has appended a new path for COLLECT_QUIET_MS), or until
// COLLECT_MAX_MS total has elapsed, whichever comes first.
void WaitForQueueToSettle(const wchar_t* queueFileName) {
    std::wstring queuePath = GetTempFilePath(queueFileName);
    DWORD elapsedMs = 0;
    DWORD quietMs = 0;
    LONGLONG lastSize = -1;

    while (elapsedMs < COLLECT_MAX_MS) {
        Sleep(COLLECT_POLL_MS);
        elapsedMs += COLLECT_POLL_MS;

        WIN32_FILE_ATTRIBUTE_DATA data{};
        LONGLONG curSize = 0;
        if (GetFileAttributesExW(queuePath.c_str(), GetFileExInfoStandard, &data)) {
            curSize = (static_cast<LONGLONG>(data.nFileSizeHigh) << 32) | static_cast<LONGLONG>(data.nFileSizeLow);
        }

        if (curSize != lastSize) {
            lastSize = curSize;
            quietMs = 0;
        } else {
            quietMs += COLLECT_POLL_MS;
            if (quietMs >= COLLECT_QUIET_MS) {
                return;
            }
        }
    }
}

std::vector<fs::path> DrainQueue(const wchar_t* queueFileName, bool filterSubtitleExt) {
    std::wstring queuePath = GetTempFilePath(queueFileName);
    std::vector<fs::path> result;

    HANDLE hFile = CreateFileW(queuePath.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return result;
    }

    std::wstring fileText;
    wchar_t buf[2048];
    DWORD read = 0;
    while (ReadFile(hFile, buf, sizeof(buf), &read, nullptr) && read > 0) {
        size_t chars = static_cast<size_t>(read / sizeof(wchar_t));
        fileText.append(buf, buf + chars);
    }
    CloseHandle(hFile);
    DeleteFileW(queuePath.c_str());

    std::wstringstream stream(fileText);
    std::wstring line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == L'\r') { line.pop_back(); }
        line = Trim(line);
        if (!line.empty()) {
            fs::path p(line);
            if (!filterSubtitleExt || IsSubtitleExtension(p.extension().wstring())) {
                result.push_back(p);
            }
        }
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Conversion engine
// ─────────────────────────────────────────────────────────────────────────────

struct EngineResult {
    bool launched = false;
    std::wstring error;
    int success = 0;
    std::vector<std::wstring> failures; // "name: reason"
};

// SUBCONVERTER_ENGINE overrides the engine path (for development); otherwise
// it sits next to this executable as subConverterEngine\subConverterEngine.exe.
std::wstring ResolveEnginePath() {
    const wchar_t* overridePath = _wgetenv(L"SUBCONVERTER_ENGINE");
    if (overridePath && *overridePath) {
        return overridePath;
    }
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    return (fs::path(self).parent_path() / L"subConverterEngine" / L"subConverterEngine.exe").wstring();
}

bool WriteUtf8Lines(const std::wstring& path, const std::vector<fs::path>& lines) {
    std::string data;
    for (const auto& line : lines) {
        data += WideToUtf8(line.wstring());
        data += "\n";
    }
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &wrote, nullptr);
    CloseHandle(h);
    return ok && wrote == data.size();
}

std::string ReadWholeFile(const std::wstring& path) {
    std::string data;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return data;
    }
    char buf[4096];
    DWORD read = 0;
    while (ReadFile(h, buf, sizeof(buf), &read, nullptr) && read > 0) {
        data.append(buf, read);
    }
    CloseHandle(h);
    return data;
}

EngineResult RunEngine(const std::vector<fs::path>& files, const std::wstring& target) {
    EngineResult result;
    std::wstring enginePath = ResolveEnginePath();
    if (GetFileAttributesW(enginePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        result.error = L"변환 엔진을 찾을 수 없습니다:\n" + enginePath;
        return result;
    }

    std::wstring tag = std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
    std::wstring listPath = GetTempFilePath(L"subConverter_list_" + tag + L".txt");
    std::wstring resultPath = GetTempFilePath(L"subConverter_result_" + tag + L".txt");
    if (!WriteUtf8Lines(listPath, files)) {
        result.error = L"작업 목록 파일을 만들지 못했습니다.";
        return result;
    }

    std::wstring cmd = L"\"" + enginePath + L"\" --to " + target +
                       L" --list \"" + listPath + L"\" --result \"" + resultPath + L"\"";
    DebugLog(L"engine: " + cmd);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        DeleteFileW(listPath.c_str());
        result.error = L"변환 엔진을 실행하지 못했습니다. (오류 " + std::to_wstring(GetLastError()) + L")";
        return result;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    result.launched = true;

    std::wstring report = Utf8ToWide(ReadWholeFile(resultPath));
    DeleteFileW(listPath.c_str());
    DeleteFileW(resultPath.c_str());

    std::wstringstream lines(report);
    std::wstring line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        size_t tab = line.find(L'\t');
        if (tab == std::wstring::npos) continue;
        std::wstring status = line.substr(0, tab);
        std::wstring rest = line.substr(tab + 1);
        if (status == L"OK") {
            ++result.success;
        } else {
            size_t tab2 = rest.find(L'\t');
            std::wstring name = rest.substr(0, tab2);
            std::wstring reason = (tab2 == std::wstring::npos) ? L"" : rest.substr(tab2 + 1);
            result.failures.push_back(name + L": " + reason);
        }
    }

    if (result.success == 0 && result.failures.empty() && exitCode != 0) {
        result.error = L"변환 엔진이 비정상 종료되었습니다. (코드 " + std::to_wstring(exitCode) + L")";
    }
    return result;
}

std::wstring JoinFailures(const std::vector<std::wstring>& failed) {
    std::wstring joined;
    for (size_t i = 0; i < failed.size(); ++i) {
        joined += failed[i];
        if (i + 1 < failed.size()) {
            joined += L"\n";
        }
    }
    return joined;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool silentMode = false;
    std::wstring target = L"srt";
    DebugLog(L"launch argc=" + std::to_wstring(argc));

    if (!argv || argc <= 1) {
        MessageBoxW(nullptr,
            L"사용법:\n파일 탐색기에서 .smi/.srt/.ass 파일을 선택하고 subConverter 메뉴에서 변환하세요.",
            L"subConverter",
            MB_OK | MB_ICONINFORMATION);
        if (argv) {
            LocalFree(argv);
        }
        return 0;
    }

    std::vector<fs::path> inputFiles;
    inputFiles.reserve(static_cast<size_t>(argc));
    int skippedCount = 0;

    for (int i = 1; i < argc; ++i) {
        DebugLog(L"arg=" + std::wstring(argv[i]));
        std::wstring arg = ToLowerW(argv[i]);
        if (arg == L"/silent" || arg == L"-silent" || arg == L"--silent") {
            silentMode = true;
            continue;
        }

        std::wstring parsedTarget = ParseTargetFormat(argv[i]);
        if (!parsedTarget.empty()) {
            target = parsedTarget;
            continue;
        }

        fs::path path(argv[i]);
        if (IsSubtitleExtension(path.extension().wstring())) {
            inputFiles.push_back(path);
        } else {
            ++skippedCount;
        }
    }
    LocalFree(argv);

    if (inputFiles.empty()) {
        DebugLog(L"inputFiles=0");
        return 0;
    }

    // Explorer may launch one process per selected file when command uses %1.
    // Aggregate those launches into one batch so users get one summary result.
    HANDLE collectorMutex = CreateMutexW(nullptr, TRUE, COLLECTOR_MUTEX_NAME);
    if (collectorMutex) {
        DWORD lastErr = GetLastError();
        if (lastErr == ERROR_ALREADY_EXISTS) {
            EnqueueFiles(inputFiles, QUEUE_WRITE_MUTEX_NAME, QUEUE_FILE_NAME);
            CloseHandle(collectorMutex);
            return 0;
        }

        EnqueueFiles(inputFiles, QUEUE_WRITE_MUTEX_NAME, QUEUE_FILE_NAME);
        WaitForQueueToSettle(QUEUE_FILE_NAME);
        inputFiles = DrainQueue(QUEUE_FILE_NAME, true);

        ReleaseMutex(collectorMutex);
        CloseHandle(collectorMutex);

        if (inputFiles.empty()) {
            return 0;
        }
    }

    DebugLog(L"inputFiles=" + std::to_wstring(inputFiles.size()));

    EngineResult engine = RunEngine(inputFiles, target);
    if (!engine.error.empty()) {
        DebugLog(L"engine error: " + engine.error);
        if (!silentMode) {
            MessageBoxW(nullptr, engine.error.c_str(), L"subConverter", MB_OK | MB_ICONERROR);
        }
        return 1;
    }

    std::vector<std::wstring> failedFiles = engine.failures;
    std::sort(failedFiles.begin(), failedFiles.end());

    int successCount = engine.success;
    int fail = static_cast<int>(failedFiles.size());
    DebugLog(L"result success=" + std::to_wstring(successCount) + L" fail=" + std::to_wstring(fail));
    if (successCount == 0 && fail == 0) {
        return 0;
    }

    std::wstring summary;
    if (successCount > 0) {
        summary += L"성공: " + std::to_wstring(successCount) + L"개";
    }
    if (fail > 0) {
        if (!summary.empty()) {
            summary += L"\n";
        }
        summary += L"실패: " + std::to_wstring(fail) + L"개";
        summary += L"\n\n" + JoinFailures(failedFiles);
    }
    if (skippedCount > 0) {
        summary += L"\n";
        summary += L"건너뜀(자막 아님): " + std::to_wstring(skippedCount) + L"개";
    }

    if (!silentMode) {
        MessageBoxW(nullptr,
            summary.c_str(),
            L"subConverter 변환 결과",
            MB_OK | (fail > 0 ? MB_ICONWARNING : MB_ICONINFORMATION));
    }

    return fail == 0 ? 0 : 1;
}
