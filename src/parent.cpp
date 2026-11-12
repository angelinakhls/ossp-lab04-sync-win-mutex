#include "parent.h"
#include "logger.h"
#include "participant.h"
#include "shared_memory.h"
#include "sync.h"
#include <cstdio>
#include <vector>

namespace lab {
namespace {

std::string ExePath() {
    char p[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    return p;
}

bool Launch(const Config& cfg, int role, PROCESS_INFORMATION& pi) {
    char args[512];
    snprintf(args, sizeof(args),
             "--role %d --ns %lu --timeout %d --ops %d --seed %u --hold %d --period %d --work %d "
             "--think %d --holder-timeout %d --crash-at %d --give-up %d%s%s%s%s",
             role, cfg.ns, cfg.timeoutMs, cfg.ops, cfg.seed, cfg.holdMs, cfg.periodMs, cfg.workMs,
             cfg.thinkMs, cfg.holderTimeoutMs, cfg.crashAt, cfg.giveUpBlocked,
             cfg.crash ? " --crash" : "", cfg.noSync ? " --no-sync" : "",
             cfg.noRecover ? " --no-recover" : "", cfg.verbose ? " --verbose" : "");
    std::string cmd = "\"" + ExePath() + "\" " + args;
    std::vector<char> buf(cmd.begin(), cmd.end());
    buf.push_back('\0');

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        fprintf(stderr, "CreateProcess failed: %lu\n", GetLastError());
        return false;
    }
    return true;
}

void AppendCsv(const Config& cfg, const char* row) {
    const char* header =
        "config,run,timeout_ms,ops,seed,hold_ms,crash,no_sync,no_recover,elapsed_ms,"
        "holder_holds,w_success,w_attempts,w_timeouts,w_abandoned,recoveries,blocked,"
        "avg_wait_ms,max_wait_ms,final_counter,expected_counter,violations,invariants_ok,max_time_exceeded\n";
    FILE* f = fopen(cfg.csv.c_str(), "a+");
    if (!f) { fprintf(stderr, "cannot open csv '%s'\n", cfg.csv.c_str()); return; }
    fseek(f, 0, SEEK_END);
    if (ftell(f) == 0) fputs(header, f);
    fputs(row, f);
    fclose(f);
}

} // namespace

int RunParent(const Config& cfgIn, RunResult* out) {
    Config cfg = cfgIn;
    FILE* gLog = cfg.logFile.empty() ? nullptr : fopen(cfg.logFile.c_str(), "w");
#define REPORT_OUT(...) do { printf(__VA_ARGS__); if (gLog) fprintf(gLog, __VA_ARGS__); } while (0)
    cfg.ns = GetCurrentProcessId();

    SharedMemory shm;
    if (!shm.Create(ObjectName("Shm", cfg.ns))) return 10;
    SharedData* d = shm.Data();

    HANDLE hMutex = CreateNamedMutex(ObjectName("Mutex", cfg.ns));
    HANDLE hStart = CreateEventA(nullptr, TRUE, FALSE, ObjectName("Start", cfg.ns).c_str());
    if (hMutex == nullptr || hStart == nullptr) {
        fprintf(stderr, "cannot create sync objects: %lu\n", GetLastError());
        if (hMutex) CloseHandle(hMutex);
        if (hStart) CloseHandle(hStart);
        return 10;
    }

    PROCESS_INFORMATION pi[kParticipants] = {};
    HANDLE procs[kParticipants] = {};
    int launched = 0;
    bool launchFailed = false;
    for (int i = 0; i < kParticipants; ++i) {
        if (!Launch(cfg, i + 1, pi[i])) { launchFailed = true; break; }
        CloseHandle(pi[i].hThread);
        procs[i] = pi[i].hProcess;
        ++launched;
    }

    bool maxExceeded = false;
    LONG64 elapsedMs = 0;
    DWORD exitCodes[kParticipants] = {};

    if (launchFailed) {
        for (int i = 0; i < launched; ++i) TerminateProcess(procs[i], 1);
    } else {
        LARGE_INTEGER q0, q1, fq;
        QueryPerformanceFrequency(&fq);
        QueryPerformanceCounter(&q0);
        d->startQpc = q0.QuadPart;
        Log().Init(ROLE_PARENT, d->startQpc, cfg.verbose);
        Log().Event(true, "EXPERIMENT START config=%s run=%d timeout=%d ops=%d seed=%u hold=%d crash=%d nosync=%d norecover=%d",
                    cfg.config.c_str(), cfg.run, cfg.timeoutMs, cfg.ops, cfg.seed, cfg.holdMs,
                    cfg.crash, cfg.noSync, cfg.noRecover);
        SetEvent(hStart);

        DWORD w = WaitForMultipleObjects(kParticipants, procs, TRUE, (DWORD)cfg.maxTimeMs);
        QueryPerformanceCounter(&q1);
        elapsedMs = (q1.QuadPart - q0.QuadPart) * 1000 / fq.QuadPart;
        if (w == WAIT_TIMEOUT || w == WAIT_FAILED) {
            maxExceeded = true;
            Log().Event(true, "MAX EXPERIMENT TIME EXCEEDED: terminating participants");
            for (int i = 0; i < kParticipants; ++i) TerminateProcess(procs[i], 98);
            WaitForMultipleObjects(kParticipants, procs, TRUE, 5000);
        }
        Log().Event(true, "ALL PARTICIPANTS FINISHED elapsed=%lld ms", (long long)elapsedMs);
    }

    for (int i = 0; i < launched; ++i) {
        if (!GetExitCodeProcess(procs[i], &exitCodes[i])) exitCodes[i] = 1;
    }

    // ---- Итоговая проверка инвариантов (все участники уже завершены) ----
    int problems = launchFailed ? 1 : 0;
    long long lateRecoveries = 0;

    // Инвариант 5: после завершения всех участников мьютекс не удерживается.
    if (!launchFailed) {
        DWORD r = WaitForSingleObject(hMutex, 0);
        if (r == WAIT_OBJECT_0) {
            ReleaseMutex(hMutex);
        } else if (r == WAIT_ABANDONED) {
            // Владелец погиб, а другие участники это уже не наблюдали: родитель восстанавливает структуру сам.
            Log().Event(true, "PARENT: WAIT_ABANDONED at final check -> recovery");
            d->counter = d->shadow; d->dirty = 0; d->owner = 0; d->state = STATE_OK;
            InterlockedIncrement(&d->epoch);
            ++lateRecoveries;
            ReleaseMutex(hMutex);
        } else {
            Log().Event(true, "INVARIANT VIOLATION: mutex still held after all participants finished");
            ++problems;
        }
    }

    LONG64 holds = d->stats[0].success;
    LONG64 wSucc = d->stats[1].success + d->stats[2].success;
    LONG64 wAtt  = d->stats[1].attempts + d->stats[2].attempts;
    LONG64 wTo   = d->stats[1].timeouts + d->stats[2].timeouts;
    LONG64 abnd  = d->stats[0].abandoned + d->stats[1].abandoned + d->stats[2].abandoned;
    LONG64 rec   = d->stats[0].recoveries + d->stats[1].recoveries + d->stats[2].recoveries + lateRecoveries;
    LONG64 blk   = d->stats[0].blocked + d->stats[1].blocked + d->stats[2].blocked;
    LONG64 waitUs = d->stats[1].waitTotalUs + d->stats[2].waitTotalUs;
    LONG64 maxUs  = d->stats[1].waitMaxUs > d->stats[2].waitMaxUs ? d->stats[1].waitMaxUs : d->stats[2].waitMaxUs;
    double avgWaitMs = wAtt ? (double)waitUs / wAtt / 1000.0 : 0.0;
    LONG64 expected = holds + wSucc;      // все ЗАВЕРШЁННЫЕ операции
    LONG64 finalCounter = d->counter;

    long long violations = d->violations;
    if (!cfg.noRecover) {
        if (d->state != STATE_OK)                { Log().Event(true, "INVARIANT VIOLATION: state != OK at end"); ++violations; }
        if (d->dirty != 0 || d->counter != d->shadow) { Log().Event(true, "INVARIANT VIOLATION: dirty/inconsistent at end"); ++violations; }
        if (finalCounter != expected)            { Log().Event(true, "INVARIANT VIOLATION: lost updates counter=%lld expected=%lld", (long long)finalCounter, (long long)expected); ++violations; }
        if (d->owner != 0)                       { Log().Event(true, "INVARIANT VIOLATION: owner != 0 at end"); ++violations; }
    } else {
        // Режим карантина: структура должна остаться нетронутой с момента объявления CORRUPTED.
        if (d->state != STATE_CORRUPTED && d->crashed)  { Log().Event(true, "INVARIANT VIOLATION: quarantine was lost"); ++violations; }
        if (d->crashed && finalCounter != d->quarantineCounter) { Log().Event(true, "INVARIANT VIOLATION: quarantined structure was modified"); ++violations; }
        Log().Event(true, "PARENT: re-initialisation of quarantined structure");
        d->counter = d->shadow; d->dirty = 0; d->owner = 0; d->state = STATE_OK; InterlockedIncrement(&d->epoch);
        ++rec;
    }
    // Инвариант 6: код завершения процессов и согласованность теста аварии.
    for (int i = 0; i < launched; ++i) {
        bool expectedCrash = (i == 0 && d->crashed);
        if (expectedCrash ? exitCodes[i] != (DWORD)kCrashExitCode : exitCodes[i] != 0) {
            Log().Event(true, "INVARIANT VIOLATION: P%d exit code %lu", i + 1, exitCodes[i]);
            ++violations;
        }
    }
    if (cfg.crash && !cfg.noSync && d->crashed && abnd + lateRecoveries != 1) {
        Log().Event(true, "INVARIANT VIOLATION: WAIT_ABANDONED observed %lld times (expected 1)", (long long)abnd);
        ++violations;
    }
    if (maxExceeded) ++violations;
    violations += problems;

    bool ok = (violations == 0);
    REPORT_OUT("\n===== RESULT (config=%s run=%d) =====\n", cfg.config.c_str(), cfg.run);
    REPORT_OUT("timeout=%d ms  ops/worker=%d  hold=%d ms  crash=%d  no-sync=%d  no-recover=%d  seed=%u\n",
           cfg.timeoutMs, cfg.ops, cfg.holdMs, cfg.crash, cfg.noSync, cfg.noRecover, cfg.seed);
    REPORT_OUT("elapsed             : %lld ms%s\n", (long long)elapsedMs, maxExceeded ? "  (MAX TIME EXCEEDED)" : "");
    REPORT_OUT("holder holds        : %lld\n", (long long)holds);
    REPORT_OUT("workers success     : %lld (P2=%lld, P3=%lld)\n", (long long)wSucc,
           (long long)d->stats[1].success, (long long)d->stats[2].success);
    REPORT_OUT("workers attempts    : %lld\n", (long long)wAtt);
    REPORT_OUT("WAIT_TIMEOUT        : %lld\n", (long long)wTo);
    REPORT_OUT("WAIT_ABANDONED      : %lld\n", (long long)abnd);
    REPORT_OUT("recoveries          : %lld   refused (structure unusable): %lld\n", (long long)rec, (long long)blk);
    REPORT_OUT("avg wait (workers)  : %.3f ms   max wait: %.3f ms\n", avgWaitMs, maxUs / 1000.0);
    REPORT_OUT("final counter       : %lld   expected: %lld\n", (long long)finalCounter, (long long)expected);
    REPORT_OUT("INVARIANT VIOLATIONS: %lld   => %s\n", violations, ok ? "PASS" : "FAIL");

    if (out) {
        out->elapsedMs = elapsedMs; out->holds = holds; out->wSuccess = wSucc; out->wAttempts = wAtt;
        out->timeouts = wTo; out->abandoned = abnd; out->recoveries = rec; out->blocked = blk;
        out->violations = violations; out->ok = ok; out->maxExceeded = maxExceeded;
    }
    if (!cfg.csv.empty()) {
        char row[512];
        snprintf(row, sizeof(row), "%s,%d,%d,%d,%u,%d,%d,%d,%d,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%.3f,%.3f,%lld,%lld,%lld,%d,%d\n",
                 cfg.config.c_str(), cfg.run, cfg.timeoutMs, cfg.ops, cfg.seed, cfg.holdMs,
                 cfg.crash, cfg.noSync, cfg.noRecover, (long long)elapsedMs, (long long)holds,
                 (long long)wSucc, (long long)wAtt, (long long)wTo, (long long)abnd, (long long)rec,
                 (long long)blk, avgWaitMs, maxUs / 1000.0, (long long)finalCounter,
                 (long long)expected, violations, ok ? 1 : 0, maxExceeded ? 1 : 0);
        AppendCsv(cfg, row);
    }

    for (int i = 0; i < launched; ++i) CloseHandle(procs[i]);
    CloseHandle(hStart);
    CloseHandle(hMutex);
    shm.Close();
    if (gLog) fclose(gLog);
    return ok ? 0 : 1;
}

} // namespace lab
