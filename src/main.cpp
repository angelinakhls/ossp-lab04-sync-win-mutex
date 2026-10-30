#include "sync.h"

#include <iostream>

HANDLE ProcessSync::CreateNamedMutex(const std::wstring& name) {
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, name.c_str());
    if (hMutex == nullptr) {
        std::wcerr << L"CreateMutexW failed: " << GetLastError() << std::endl;
    }
    return hMutex;
}

DWORD ProcessSync::WaitWithTimeout(HANDLE hMutex, DWORD timeoutMs, bool& abandoned) {
    if (hMutex == nullptr) {
        abandoned = false;
        return WAIT_FAILED;
    }

    DWORD result = WaitForSingleObject(hMutex, timeoutMs);
    if (result == WAIT_OBJECT_0) {
        abandoned = false;
        return WAIT_OBJECT_0;
    }
    if (result == WAIT_TIMEOUT) {
        abandoned = false;
        return WAIT_TIMEOUT;
    }
    if (result == WAIT_ABANDONED) {
        abandoned = true;
        return WAIT_ABANDONED;
    }

    abandoned = false;
    return WAIT_FAILED;
}

bool ProcessSync::ReleaseMutexSafe(HANDLE hMutex) {
    if (hMutex == nullptr) return false;
    return ReleaseMutex(hMutex) != FALSE;
}
