#pragma once

#include <windows.h>
#include <string>

class ProcessSync {
public:
    static HANDLE CreateNamedMutex(const std::wstring& name);
    static DWORD WaitWithTimeout(HANDLE hMutex, DWORD timeoutMs, bool& abandoned);
    static bool ReleaseMutexSafe(HANDLE hMutex);
};
