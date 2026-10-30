#include <windows.h>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <chrono>

#include "logger.h"
#include "shared_memory.h"
#include "sync.h"

constexpr const wchar_t* kSharedMemoryName = L"Lab04SharedMemoryVariant28";
constexpr const wchar_t* kMutexName = L"Lab04MutexVariant28";

struct ProgramConfig {
    int timeoutMs = 10;
    bool startOwnerCrash = false;
    bool emergencyRecovery = true;
    int iterations = 40;
    int ownerHoldMs = 50;
};

static void LogInvariantValue(const std::string& label, LONG value) {
    std::ostringstream oss;
    oss << label << "=" << value;
    Logger::Instance().Log(oss.str());
}

static bool ValidateState(const SharedData* data) {
    if (!data) return false;
    if (data->corruptedFlag != 0) return false;
    if (data->mutexOwner < 0 || data->mutexOwner > 3) return false;
    return true;
}

static int RunOwnerProcess(const ProgramConfig& cfg) {
    HANDLE hMutex = ProcessSync::CreateNamedMutex(kMutexName);
    if (!hMutex) {
        Logger::Instance().Log("OWNER: failed to create mutex");
        return 1;
    }

    SharedMemory shm(kSharedMemoryName, sizeof(SharedData));
    if (!shm.Open()) {
        if (!shm.Create()) {
            Logger::Instance().Log("OWNER: cannot initialize shared memory");
            return 2;
        }
    }

    int cycle = 0;
    while (cycle < cfg.iterations) {
        DWORD waitResult = WaitForSingleObject(hMutex, INFINITE);
        if (waitResult != WAIT_OBJECT_0) {
            Logger::Instance().Log("OWNER: failed to obtain mutex");
            break;
        }

        SharedData* data = shm.Data();
        if (!data) {
            Logger::Instance().Log("OWNER: null shared memory");
            ReleaseMutex(hMutex);
            break;
        }

        data->mutexOwner = 1;
        data->lockState = 1;
        data->sequenceNumber++;
        data->valueA = 100 + cycle;
        data->valueB = 200 + cycle;
        data->valueC = 300 + cycle;
        data->lastWriter = 1;
        data->lastOperationMs = static_cast<LONG>(GetTickCount64() % 60000);
        Logger::Instance().Log("OWNER: writing values and holding mutex for 50 ms");

        std::this_thread::sleep_for(std::chrono::milliseconds(cfg.ownerHoldMs));

        if (cfg.startOwnerCrash && cycle == 1) {
            Logger::Instance().Log("OWNER: crash simulation before release -> mutex abandoned");
            CloseHandle(hMutex);
            return 0;
        }

        data->mutexOwner = 0;
        data->lockState = 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        ReleaseMutex(hMutex);
        Logger::Instance().Log("OWNER: released mutex");
        ++cycle;
    }

    CloseHandle(hMutex);
    return 0;
}

static int RunClientProcess(const ProgramConfig& cfg, int clientId) {
    HANDLE hMutex = OpenMutexW(SYNCHRONIZE, FALSE, kMutexName);
    if (!hMutex) {
        Logger::Instance().Log("CLIENT " + std::to_string(clientId) + ": failed to open mutex");
        return 1;
    }

    SharedMemory shm(kSharedMemoryName, sizeof(SharedData));
    if (!shm.Open()) {
        Logger::Instance().Log("CLIENT " + std::to_string(clientId) + ": failed to open memory");
        return 2;
    }

    for (int i = 0; i < cfg.iterations; ++i) {
        bool abandoned = false;
        DWORD start = GetTickCount();
        DWORD waitResult = ProcessSync::WaitWithTimeout(hMutex, cfg.timeoutMs, abandoned);
        DWORD elapsed = GetTickCount() - start;

        if (waitResult == WAIT_OBJECT_0) {
            SharedData* data = shm.Data();
            if (!data) {
                Logger::Instance().Log("CLIENT " + std::to_string(clientId) + ": invalid shared pointer");
                ProcessSync::ReleaseMutexSafe(hMutex);
                continue;
            }

            LogInvariantValue("CLIENT " + std::to_string(clientId) + " before read valueA", data->valueA);
            if (!ValidateState(data)) {
                Logger::Instance().Log("CLIENT " + std::to_string(clientId) + ": invariant violation, recovering data");
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
                Logger::Instance().Log("CLIENT " + std::to_string(clientId) + ": data inconsistency detected; corruption flag set");
                shm.MarkCorrupted();
            }

            ProcessSync::ReleaseMutexSafe(hMutex);
            Logger::Instance().Log("CLIENT " + std::to_string(clientId) + ": acquired and released mutex");
        }
        else if (waitResult == WAIT_TIMEOUT) {
            Logger::Instance().Log("CLIENT " + std::to_string(clientId) + ": WAIT_TIMEOUT elapsed=" + std::to_string(elapsed));
        }
        else if (waitResult == WAIT_ABANDONED) {
            Logger::Instance().Log("CLIENT " + std::to_string(clientId) + ": WAIT_ABANDONED - recovering shared state");
            shm.Recover();
        }
        else {
            Logger::Instance().Log("CLIENT " + std::to_string(clientId) + ": WAIT_FAILED");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    CloseHandle(hMutex);
    return 0;
}

static void SpawnChild(const std::string& mode, const ProgramConfig& cfg) {
    std::wstring commandLine = L"lab04_sync.exe --mode " + std::wstring(mode.begin(), mode.end()) + L" --timeout " + std::to_wstring(cfg.timeoutMs);
    STARTUPINFOW si = {};
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessW(
        NULL,
        commandLine.data(),
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &si,
        &pi)) {
        std::wcerr << L"CreateProcessW failed: " << GetLastError() << std::endl;
        return;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

static int ParentProcess(const ProgramConfig& cfg) {
    SharedMemory shm(kSharedMemoryName, sizeof(SharedData));
    if (!shm.Create()) {
        if (!shm.Open()) {
            Logger::Instance().Log("PARENT: failed to initialize shared memory");
            return 1;
        }
    }
    shm.Reset();

    Logger::Instance().Log("PARENT: launching owner and clients");
    SpawnChild("owner", cfg);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    SpawnChild("client1", cfg);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    SpawnChild("client2", cfg);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    return 0;
}

int main(int argc, char* argv[]) {
    ProgramConfig cfg;
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
            cfg.startOwnerCrash = true;
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

    int res = ParentProcess(cfg);
    auto events = Logger::Instance().GetEvents();
    std::cout << "=== Event log ===" << std::endl;
    for (const auto& ev : events) {
        std::cout << ev << std::endl;
    }
    return res;
}
