// suite.h - автоматические наборы запусков: проверки корректности и эксперименты.
#pragma once
#include "common.h"
namespace lab {
// name: "tests" или "experiments". Возвращает 0, если все проверки пройдены.
int RunSuite(const Config& base, const std::string& name);
}
