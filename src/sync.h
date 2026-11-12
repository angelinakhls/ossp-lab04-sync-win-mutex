// sync.h - обёртки над Mutex: единая обработка кодов WaitForSingleObject.
#pragma once
#include "common.h"

namespace lab {

enum class Acquire { Acquired, Timeout, Abandoned, Failed };

HANDLE CreateNamedMutex(const std::string& name);   // родитель
HANDLE OpenNamedMutex(const std::string& name);     // потомки

// WAIT_OBJECT_0 -> Acquired; WAIT_TIMEOUT -> Timeout;
// WAIT_ABANDONED -> Abandoned (мьютекс ПЕРЕДАН вызывающему!); иначе Failed.
Acquire AcquireMutex(HANDLE h, DWORD timeoutMs, DWORD* lastError);
bool ReleaseChecked(HANDLE h);

} // namespace lab
