#include <windows.h>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>

#include "logger.h"
#include "shared_memory.h"
#include "sync.h"

constexpr const wchar_t* kSharedMemoryName = L"Lab04SharedMemoryVariant28";
constexpr const wchar_t* kMutexName = L"Lab04MutexVariant28";

struct Config {
    int timeoutMs = 10;
    bool crashOwner = false;
    int ownerHoldMs = 50;
    int iterations = 40;
};

static std::wstring ToWide(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], len);
    return out;
}

static std::wstring GetExecutablePath() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::wstring(path);
}

static bool ValidateInvariant(const SharedData* data) {
    if (data == nullptr) return false;
    if (data->corruptedFlag != 0) return false;
    if (data->mutexOwner < 0 || data->mutexOwner > 3) return false;
    return true;
}

static std::string FormatState(const std::string& processName, const SharedData* data) {
    std::ostringstream oss;
    oss << processName << ": mutexOwner=" << data->mutexOwner
        << ", valueA=" << data->valueA
        << ", valueB=" << data->valueB
        << ", valueC=" << data->valueC;
    return oss.str();
}

static void LaunchProcess(const std::wstring& mode, int timeoutMs, bool crashOwner) {
    std::wstring exe = GetExecutablePath();
    std::wstring cmd = L"\"" + exe + L"\" --mode " + mode + L" --timeout " + std::to_wstring(timeoutMs);
    if (crashOwner) {
        cmd += L" --crash";
    }

    std::vector<wchar_t> cmdBuffer(cmd.begin(), cmd.end());
    cmdBuffer.push_back(L'\0');

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessW(
        nullptr,
        cmdBuffer.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &si,
        &pi)) {
        std::wcerr << L"CreateProcessW failed: " << GetLastError() << std::endl;
        return;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

static int RunOwnerProcess(const Config& cfg) {
    HANDLE hMutex = ProcessSync::CreateNamedMutex(kMutexName);
    if (hMutex == nullptr) {
        Logger::Instance().Log("OWNER: CreateMutex failed");
        return 1;
    }

    SharedMemory shm(kSharedMemoryName, sizeof(SharedData));
    if (!shm.Open()) {
        if (!shm.Create()) {
            Logger::Instance().Log("OWNER: cannot initialize shared memory");
            CloseHandle(hMutex);
            return 2;
        }
    }

    for (int i = 0; i < cfg.iterations; ++i) {
        DWORD waitResult = WaitForSingleObject(hMutex, INFINITE);
        if (waitResult != WAIT_OBJECT_0) {
            Logger::Instance().Log("OWNER: failed to wait mutex");
            break;
        }

        SharedData* data = shm.Data();
        if (data == nullptr) {
            Logger::Instance().Log("OWNER: null shared data pointer");
            ReleaseMutex(hMutex);
            break;
        }

        data->mutexOwner = 1;
        data->lockState = 1;
        data->sequenceNumber++;
        data->valueA = 100 + i;
        data->valueB = 200 + i;
        data->valueC = 300 + i;
        data->lastWriter = 1;
        data->lastOperationMs = static_cast<LONG>(GetTickCount64() % 60000);

        Logger::Instance().Log("OWNER: acquired mutex and wrote shared data");
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg.ownerHoldMs));

        if (cfg.crashOwner && i == 2) {
            Logger::Instance().Log("OWNER: simulated crash before ReleaseMutex -> mutex abandoned");
            CloseHandle(hMutex);
            return 0;
        }

        data->mutexOwner = 0;
        data->lockState = 0;
        ReleaseMutex(hMutex);
        Logger::Instance().Log("OWNER: released mutex");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    CloseHandle(hMutex);
    return 0;
}

static int RunClientProcess(const Config& cfg, int clientId) {
    HANDLE hMutex = OpenMutexW(SYNCHRONIZE, FALSE, kMutexName);
    if (hMutex == nullptr) {
        Logger::Instance().Log("CLIENT " + std::to_string(clientId) + ": cannot open mutex");
        return 1;
    }

    SharedMemory shm(kSharedMemoryName, sizeof(SharedData));
    if (!shm.Open()) {
        Logger::Instance().Log("CLIENT " + std::to_string(clientId) + ": cannot open shared memory");
        CloseHandle(hMutex);
        return 2;
    }

    int timeoutCount = 0;
    int abandonedCount = 0;
    int successCount = 0;

    for (int i = 0; i < cfg.iterations; ++i) {
        bool abandoned = false;
        DWORD start = GetTickCount();
        DWORD waitResult = ProcessSync::WaitWithTimeout(hMutex, cfg.timeoutMs, abandoned);
        DWORD elapsed = GetTickCount() - start;

        if (waitResult == WAIT_OBJECT_0) {
            SharedData* data = shm.Data();
            if (data == nullptr) {
                Logger::Instance().Log("CLIENT " + std::to_string(clientId) + ": null data while holding mutex");
                ProcessSync::ReleaseMutexSafe(hMutex);
                continue;
            }

            if (data->corruptedFlag != 0 || !ValidateInvariant(data)) {
                Logger::Instance().Log("CLIENT " + std::to_string(clientId) + ": corrupted or invalid state detected; recover");
                shm.Recover();
            }

            data->valueA += clientId;
            data->valueB += clientId * 2;
            data->valueC += clientId * 3;
            data->sequenceNumber++;
            data->lastWriter = clientId;
            data->lastOperationMs = static_cast<LONG>(elapsed);
            data->mutexOwner = clientId + 1;

            if (!shm.IsConsistent()) {
                Logger::Instance().Log("CLIENT " + std::to_string(clientId) + ": inconsistency detected; mark corrupted");
                shm.MarkCorrupted();
            }

            Logger::Instance().Log("CLIENT " + std::to_string(clientId) + ": acquired mutex, updated state, then released");
            ProcessSync::ReleaseMutexSafe(hMutex);
            successCount++;
        }
        else if (waitResult == WAIT_TIMEOUT) {
            Logger::Instance().Log("CLIENT " + std::to_string(clientId) + ": WAIT_TIMEOUT after " + std::to_string(elapsed) + " ms");
            timeoutCount++;
        }
        else if (waitResult == WAIT_ABANDONED) {
            Logger::Instance().Log("CLIENT " + std::to_string(clientId) + ": WAIT_ABANDONED -> recovery");
            shm.Recover();
            abandonedCount++;
        }
        else {
            Logger::Instance().Log("CLIENT " + std::to_string(clientId) + ": WAIT_FAILED");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::ostringstream oss;
    oss << "CLIENT " << clientId << " stats: success=" << successCount
        << ", timeout=" << timeoutCount
        << ", abandoned=" << abandonedCount;
    Logger::Instance().Log(oss.str());

    CloseHandle(hMutex);
    return 0;
}

static int RunParentProcess(const Config& cfg) {
    SharedMemory shm(kSharedMemoryName, sizeof(SharedData));
    if (!shm.Create()) {
        if (!shm.Open()) {
            std::cerr << "Parent: cannot initialize shared memory" << std::endl;
            return 1;
        }
    }
    shm.Reset();

    Logger::Instance().Log("PARENT: start owner and clients");
    LaunchProcess(L"owner", cfg.timeoutMs, cfg.crashOwner);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    LaunchProcess(L"client1", cfg.timeoutMs, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    LaunchProcess(L"client2", cfg.timeoutMs, false);

    std::this_thread::sleep_for(std::chrono::milliseconds(1700));
    Logger::Instance().Log("PARENT: experiment finished");

    auto events = Logger::Instance().GetEvents();
    for (const auto& e : events) {
        std::cout << e << std::endl;
    }

    shm.Close();
    return 0;
}

int main(int argc, char* argv[]) {
    Config cfg;
    std::string mode = "parent";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--timeout" && i + 1 < argc) {
            cfg.timeoutMs = std::stoi(argv[++i]);
        }
        else if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        }
        else if (arg == "--crash") {
            cfg.crashOwner = true;
        }
    }

    if (mode == "owner") {
        return RunOwnerProcess(cfg);
    }
    if (mode == "client1") {
        return RunClientProcess(cfg, 1);
    }
    if (mode == "client2") {
        return RunClientProcess(cfg, 2);
    }

    return RunParentProcess(cfg);
}
