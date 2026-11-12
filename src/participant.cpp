#include "participant.h"
#include "logger.h"
#include "shared_memory.h"
#include "sync.h"
#include <random>
#include <cstdio>

namespace lab {
namespace {

struct Ctx {
    const Config& cfg;
    SharedData* d;
    HANDLE hMutex;
    int role;
    ProcStats* st;
};

LONG64 NowUs() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1000000 / f.QuadPart;
}

void Violation(Ctx& c, const char* what) {
    InterlockedIncrement(&c.d->violations);
    Log().Event(true, "INVARIANT VIOLATION: %s", what);
}

void AddWait(Ctx& c, LONG64 us) {
    c.st->waitTotalUs += us;
    if (us > c.st->waitMaxUs) c.st->waitMaxUs = us;
}

// Критическая секция: вызывается ТОЛЬКО когда мьютекс захвачен (или в режиме --no-sync).
// crashHere - тест аварии: процесс завершается, не вызвав ReleaseMutex.
// Возвращает true, если операция завершена.
bool CriticalSection(Ctx& c, int workMs, bool crashHere) {
    SharedData* d = c.d;

    // Инвариант 4: структуру в состоянии CORRUPTED использовать нельзя.
    if (d->state != STATE_OK) {
        c.st->blocked++;
        Log().Event(false, "STRUCTURE UNUSABLE (STATE_CORRUPTED), access refused");
        return false;
    }
    // Инвариант 1: взаимное исключение.
    if (d->owner != 0) Violation(c, "MUTUAL_EXCLUSION (owner != 0 on entry)");
    // Инвариант 2: согласованность данных при входе.
    if (d->dirty != 0 || d->counter != d->shadow) Violation(c, "INCONSISTENT_DATA_ON_ENTRY");

    d->owner = c.role;
    d->dirty = 1;                       // запись начата
    LONG64 tmp = d->counter;            // чтение
    Log().Event(false, "WORKING counter=%lld", (long long)tmp);
    if (workMs > 0) Sleep(workMs);      // моделирование длительности операции

    if (crashHere) {
        d->counter = tmp + 1;           // counter обновлён, shadow - нет, dirty == 1
        d->crashed = 1;
        Log().Event(true, "CRASH TEST: TerminateProcess while holding Mutex (data left inconsistent)");
        TerminateProcess(GetCurrentProcess(), kCrashExitCode);   // ReleaseMutex НЕ вызывается
        return false;                   // недостижимо
    }

    d->counter = tmp + 1;               // запись
    d->shadow  = d->counter;            // фиксация согласованного состояния
    d->dirty   = 0;
    d->owner   = 0;
    c.st->success++;
    return true;
}

// Обработка WAIT_ABANDONED. Мьютекс уже принадлежит нам, данные могут быть несогласованными.
void HandleAbandoned(Ctx& c) {
    SharedData* d = c.d;
    c.st->abandoned++;
    Log().Event(true, "WAIT_ABANDONED: previous owner P%ld terminated; dirty=%ld counter=%lld shadow=%lld",
                d->owner, d->dirty, (long long)d->counter, (long long)d->shadow);

    InterlockedExchange(&d->state, STATE_CORRUPTED);   // структура объявлена непригодной
    d->quarantineCounter = d->counter;

    if (c.cfg.noRecover) {
        Log().Event(true, "STRUCTURE QUARANTINED: recovery disabled (--no-recover)");
        return;                                        // мьютекс отпускается вызывающим
    }
    // Восстановление: откат к последнему согласованному состоянию (shadow).
    d->counter = d->shadow;
    d->dirty   = 0;
    d->owner   = 0;
    InterlockedIncrement(&d->epoch);
    InterlockedExchange(&d->state, STATE_OK);
    c.st->recoveries++;
    Log().Event(true, "RECOVERED: structure re-initialised to counter=%lld epoch=%ld",
                (long long)d->counter, d->epoch);
}

// Одна попытка доступа. Возвращает: 1 - операция выполнена, 0 - нет, -1 - фатальная ошибка.
int Attempt(Ctx& c, DWORD timeoutMs, int workMs, bool crashHere) {
    c.st->attempts++;
    if (c.cfg.noSync) {                                   // контрольный режим без Mutex
        return CriticalSection(c, workMs, crashHere) ? 1 : 0;
    }

    Log().Event(false, "WAITING RESOURCE=Mutex timeout=%lu ms", timeoutMs);
    LONG64 t0 = NowUs();
    DWORD err = 0;
    Acquire r = AcquireMutex(c.hMutex, timeoutMs, &err);
    AddWait(c, NowUs() - t0);

    switch (r) {
        case Acquire::Acquired: {
            Log().Event(false, "ACQUIRED RESOURCE=Mutex");
            bool ok = CriticalSection(c, workMs, crashHere);
            ReleaseChecked(c.hMutex);
            Log().Event(false, "RELEASED RESOURCE=Mutex");
            return ok ? 1 : 0;
        }
        case Acquire::Timeout:
            c.st->timeouts++;
            Log().Event(false, "WAIT_TIMEOUT after %lu ms", timeoutMs);
            return 0;
        case Acquire::Abandoned:
            HandleAbandoned(c);
            ReleaseChecked(c.hMutex);
            Log().Event(true, "RELEASED RESOURCE=Mutex (after WAIT_ABANDONED)");
            return 0;
        default:
            Log().Event(true, "WAIT_FAILED error=%lu", err);
            return -1;
    }
}

int RunHolder(Ctx& c) {
    Log().Event(true, "STATE=STARTED role=HOLDER hold=%d ms period=%d ms", c.cfg.holdMs, c.cfg.periodMs);
    int exitCode = 0;
    for (int cycle = 1; c.d->workersDone < 2; ++cycle) {
        bool crashHere = c.cfg.crash && cycle == c.cfg.crashAt;
        int r = Attempt(c, (DWORD)c.cfg.holderTimeoutMs, c.cfg.holdMs, crashHere);
        if (r < 0) { exitCode = 4; break; }
        if (c.cfg.noRecover && c.st->blocked >= (LONG64)c.cfg.giveUpBlocked) break;
        Sleep((DWORD)c.cfg.periodMs);                     // пауза между удержаниями
    }
    Log().Event(true, "STATE=FINISHED holds=%lld timeouts=%lld", (long long)c.st->success,
                (long long)c.st->timeouts);
    return exitCode;
}

int RunWorker(Ctx& c) {
    Log().Event(true, "STATE=STARTED role=WORKER timeout=%d ms ops=%d", c.cfg.timeoutMs, c.cfg.ops);
    std::mt19937 rng(c.cfg.seed + (unsigned)c.role * 7919u);   // воспроизводимые паузы
    std::uniform_int_distribution<int> think(0, c.cfg.thinkMs > 0 ? c.cfg.thinkMs : 0);
    int exitCode = 0;

    while (c.st->success < c.cfg.ops) {
        int r = Attempt(c, (DWORD)c.cfg.timeoutMs, c.cfg.workMs, false);
        if (r < 0) { exitCode = 4; break; }
        if (c.cfg.noRecover && c.st->blocked >= (LONG64)c.cfg.giveUpBlocked) {
            Log().Event(true, "STATE=GIVING_UP: structure stays unusable until re-initialisation");
            break;
        }
        int pause = think(rng);
        if (pause > 0) Sleep((DWORD)pause);               // интенсивность запросов
    }
    InterlockedIncrement(&c.d->workersDone);
    Log().Event(true, "STATE=FINISHED ops=%lld attempts=%lld timeouts=%lld abandoned=%lld",
                (long long)c.st->success, (long long)c.st->attempts,
                (long long)c.st->timeouts, (long long)c.st->abandoned);
    return exitCode;
}

} // namespace

int RunParticipant(const Config& cfg) {
    SharedMemory shm;
    if (!shm.Open(ObjectName("Shm", cfg.ns))) return 2;

    HANDLE hMutex = OpenNamedMutex(ObjectName("Mutex", cfg.ns));
    HANDLE hStart = OpenEventA(SYNCHRONIZE, FALSE, ObjectName("Start", cfg.ns).c_str());
    if (hMutex == nullptr || hStart == nullptr) {
        fprintf(stderr, "participant: cannot open sync objects (%lu)\n", GetLastError());
        if (hMutex) CloseHandle(hMutex);
        if (hStart) CloseHandle(hStart);
        return 2;
    }

    // Все участники стартуют одновременно по событию Start (Manual Reset Event).
    if (WaitForSingleObject(hStart, 10000) != WAIT_OBJECT_0) {
        fprintf(stderr, "participant: start event timeout\n");
        CloseHandle(hMutex); CloseHandle(hStart);
        return 3;
    }

    SharedData* d = shm.Data();
    Log().Init(cfg.role, d->startQpc, cfg.verbose);
    Ctx c{cfg, d, hMutex, cfg.role, &d->stats[cfg.role - 1]};

    int code = (cfg.role == ROLE_HOLDER) ? RunHolder(c) : RunWorker(c);

    CloseHandle(hStart);
    CloseHandle(hMutex);
    return code;
}

} // namespace lab
