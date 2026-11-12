#include "sync.h"
#include <cstdio>

namespace lab {

HANDLE CreateNamedMutex(const std::string& name) {
    HANDLE h = CreateMutexA(nullptr, FALSE, name.c_str());
    if (h == nullptr) fprintf(stderr, "CreateMutex failed: %lu\n", GetLastError());
    return h;
}

HANDLE OpenNamedMutex(const std::string& name) {
    HANDLE h = OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name.c_str());
    if (h == nullptr) fprintf(stderr, "OpenMutex failed: %lu\n", GetLastError());
    return h;
}

Acquire AcquireMutex(HANDLE h, DWORD timeoutMs, DWORD* lastError) {
    DWORD r = WaitForSingleObject(h, timeoutMs);
    switch (r) {
        case WAIT_OBJECT_0:  return Acquire::Acquired;
        case WAIT_TIMEOUT:   return Acquire::Timeout;
        case WAIT_ABANDONED: return Acquire::Abandoned;
        default:
            if (lastError) *lastError = GetLastError();
            return Acquire::Failed;
    }
}

bool ReleaseChecked(HANDLE h) {
    if (!ReleaseMutex(h)) {
        fprintf(stderr, "ReleaseMutex failed: %lu\n", GetLastError());
        return false;
    }
    return true;
}

} // namespace lab
