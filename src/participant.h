// participant.h - логика дочерних процессов: Holder (P1) и Worker (P2, P3).
#pragma once
#include "common.h"

namespace lab {
// Возвращает код завершения процесса: 0 - штатно, иначе ошибка.
int RunParticipant(const Config& cfg);
constexpr int kCrashExitCode = 99;   // код, с которым Holder завершается в тесте аварии
}
