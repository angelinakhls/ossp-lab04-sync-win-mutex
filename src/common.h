// common.h - общие типы: параметры запуска и структура данных в Shared Memory.
#pragma once
#include <windows.h>
#include <string>

namespace lab {

constexpr LONG kMagic = 0x4C414234;      // "LAB4" - признак инициализированной структуры
constexpr int  kParticipants = 3;        // P1 (Holder), P2 и P3 (Workers)

enum Role { ROLE_PARENT = 0, ROLE_HOLDER = 1, ROLE_WORKER_A = 2, ROLE_WORKER_B = 3 };
enum DataState : LONG { STATE_OK = 0, STATE_CORRUPTED = 1 };

// Статистика одного участника. Каждый участник пишет только в свой слот,
// родитель читает слоты после завершения всех дочерних процессов.
struct ProcStats {
    LONG64 attempts;      // число вызовов WaitForSingleObject
    LONG64 success;       // завершённые операции над структурой
    LONG64 timeouts;      // результатов WAIT_TIMEOUT
    LONG64 abandoned;     // результатов WAIT_ABANDONED
    LONG64 recoveries;    // выполненных восстановлений структуры
    LONG64 blocked;       // отказов использовать структуру (STATE_CORRUPTED)
    LONG64 waitTotalUs;   // суммарное время ожидания мьютекса, мкс
    LONG64 waitMaxUs;     // максимальное время одного ожидания, мкс
};

// Общая структура данных, размещаемая в Shared Memory.
// Поля counter/shadow/dirty/owner/state изменяются ТОЛЬКО под защитой Mutex.
struct SharedData {
    LONG   magic;
    volatile LONG state;        // STATE_OK / STATE_CORRUPTED
    volatile LONG dirty;        // 1 - запись начата, но не завершена
    volatile LONG owner;        // роль участника внутри критической секции (0 - никто)
    volatile LONG epoch;        // число восстановлений (повторных инициализаций)
    volatile LONG workersDone;  // сколько Workers завершили работу
    volatile LONG crashed;      // 1 - тестовое аварийное завершение выполнено
    volatile LONG violations;   // число обнаруженных нарушений инвариантов
    LONG64 startQpc;            // момент начала эксперимента (QueryPerformanceCounter)
    volatile LONG64 counter;    // рабочий счётчик
    volatile LONG64 shadow;     // копия counter, обновляемая в конце записи
    volatile LONG64 quarantineCounter; // значение counter в момент объявления STATE_CORRUPTED
    ProcStats stats[kParticipants];
};

struct Config {
    int  timeoutMs   = 10;     // тайм-аут ожидания мьютекса у Workers
    int  ops         = 100;    // число успешных операций каждого Worker
    unsigned seed    = 12058;  // seed генератора случайных пауз
    int  holdMs      = 50;     // время удержания мьютекса процессом Holder
    int  periodMs    = 50;     // пауза Holder между удержаниями
    int  workMs      = 1;      // длительность операции Worker внутри критической секции
    int  thinkMs     = 5;      // максимум случайной паузы Worker между попытками
    int  holderTimeoutMs = 1000; // ограниченное ожидание у Holder (защита от зависания)
    int  maxTimeMs   = 60000;  // максимальная длительность эксперимента
    int  crashAt     = 3;      // номер удержания, во время которого Holder аварийно завершается
    int  giveUpBlocked = 20;   // сколько отказов (STATE_CORRUPTED) допустимо в режиме --no-recover
    bool crash       = false;  // включить тестовое аварийное завершение
    bool noSync      = false;  // контрольный режим без Mutex (демонстрация гонки)
    bool noRecover   = false;  // не восстанавливать структуру после WAIT_ABANDONED
    bool verbose     = false;  // подробный журнал
    std::string csv;           // файл для добавления строки результатов
    std::string config = "manual";
    std::string suite;         // tests | experiments - автоматический набор запусков
    std::string logFile;       // файл для сводного отчёта запуска
    int  suiteRuns   = 3;      // число запусков каждой конфигурации в наборе
    int  run         = 1;
    int  role        = ROLE_PARENT;
    DWORD ns         = 0;      // идентификатор пространства имён (PID родителя)
};

std::string ObjectName(const char* kind, DWORD ns);

} // namespace lab
