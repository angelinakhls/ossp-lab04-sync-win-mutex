// parent.h - родительский процесс: создание объектов, запуск участников, проверка инвариантов.
#pragma once
#include "common.h"
namespace lab {

struct RunResult {
    long long elapsedMs = 0, holds = 0, wSuccess = 0, wAttempts = 0, timeouts = 0;
    long long abandoned = 0, recoveries = 0, blocked = 0, violations = 0;
    bool ok = false, maxExceeded = false;
};

int RunParent(const Config& cfg, RunResult* out = nullptr);
}
