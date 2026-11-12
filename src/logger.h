// logger.h - потокобезопасный журнал событий: время относительно начала эксперимента,
// идентификатор участника, PID/TID, тип события, ресурс.
#pragma once
#include <windows.h>

namespace lab {

class Logger {
public:
    Logger();
    ~Logger();
    void Init(int role, LONG64 startQpc, bool verbose);
    // important=true - выводится всегда; false - только при --verbose
    void Event(bool important, const char* fmt, ...);
    LONG64 ElapsedMs() const;
private:
    CRITICAL_SECTION cs_;
    LARGE_INTEGER freq_;
    LONG64 startQpc_;
    int role_;
    bool verbose_;
};

Logger& Log();

} // namespace lab
