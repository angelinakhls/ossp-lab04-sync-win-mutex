#include "logger.h"
#include <cstdio>
#include <cstdarg>

namespace lab {

Logger::Logger() : startQpc_(0), role_(0), verbose_(false) {
    InitializeCriticalSection(&cs_);
    QueryPerformanceFrequency(&freq_);
}

Logger::~Logger() { DeleteCriticalSection(&cs_); }

void Logger::Init(int role, LONG64 startQpc, bool verbose) {
    role_ = role;
    startQpc_ = startQpc;
    verbose_ = verbose;
}

LONG64 Logger::ElapsedMs() const {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (now.QuadPart - startQpc_) * 1000 / freq_.QuadPart;
}

void Logger::Event(bool important, const char* fmt, ...) {
    if (!important && !verbose_) return;

    char body[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    EnterCriticalSection(&cs_);            // журнал потокобезопасен
    char line[512];
    int n = snprintf(line, sizeof(line), "[%06lld ms] P%d PID=%lu TID=%lu %s\n",
                     ElapsedMs(), role_, GetCurrentProcessId(), GetCurrentThreadId(), body);
    if (n > 0) {                           // одна запись = один вызов WriteFile
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line, (DWORD)n, &written, nullptr);
    }
    LeaveCriticalSection(&cs_);
}

Logger& Log() {
    static Logger instance;
    return instance;
}

} // namespace lab
