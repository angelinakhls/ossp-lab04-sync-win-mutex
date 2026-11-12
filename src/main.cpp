// Лабораторная работа 4, вариант 28: тайм-аут и аварийное освобождение Mutex.
// Три процесса (Holder, Worker, Worker) работают с общей структурой в Shared Memory.
#include "common.h"
#include "parent.h"
#include "participant.h"
#include "suite.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mmsystem.h>

#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#endif

using namespace lab;

static void Usage() {
    puts(
        "Usage: lab04_sync.exe [options]\n"
        "  --timeout N      Worker wait timeout, ms (default 10)\n"
        "  --ops N          successful operations per Worker (default 100)\n"
        "  --seed N         RNG seed (default 12058 = 1000 + 114*97)\n"
        "  --hold N         Holder hold time, ms (default 50)\n"
        "  --period N       Holder pause between holds, ms (default 50)\n"
        "  --work N         Worker operation time inside critical section, ms (default 1)\n"
        "  --think N        max random Worker pause, ms (default 5)\n"
        "  --max-time N     maximum experiment time, ms (default 60000)\n"
        "  --crash          Holder terminates itself while holding the Mutex\n"
        "  --crash-at N     number of the hold during which Holder crashes (default 3)\n"
        "  --no-recover     do not recover after WAIT_ABANDONED (quarantine test)\n"
        "  --no-sync        control mode WITHOUT Mutex (demonstrates the race)\n"
        "  --csv FILE       append result row to FILE\n"
        "  --config NAME    configuration label for CSV\n"
        "  --run N          run number for CSV\n"
        "  --verbose        detailed event log\n"
        "  --suite NAME     automatic set of runs: tests | experiments (writes into data/)\n"
        "  --runs N         runs per configuration in suite (default 3)\n");
}

static bool ToInt(const char* s, int lo, int hi, int& out) {
    char* end = nullptr;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < lo || v > hi) return false;
    out = (int)v;
    return true;
}

int main(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        auto next = [&](int lo, int hi, int& out) -> bool {
            if (i + 1 >= argc || !ToInt(argv[i + 1], lo, hi, out)) {
                fprintf(stderr, "Bad value for %s\n", a);
                return false;
            }
            ++i;
            return true;
        };
        int v = 0;
        if      (!strcmp(a, "--timeout"))        { if (!next(0, 600000, cfg.timeoutMs)) return 2; }
        else if (!strcmp(a, "--ops"))            { if (!next(1, 1000000, cfg.ops)) return 2; }
        else if (!strcmp(a, "--seed"))           { if (!next(0, 2000000000, v)) return 2; cfg.seed = (unsigned)v; }
        else if (!strcmp(a, "--hold"))           { if (!next(0, 60000, cfg.holdMs)) return 2; }
        else if (!strcmp(a, "--period"))         { if (!next(0, 60000, cfg.periodMs)) return 2; }
        else if (!strcmp(a, "--work"))           { if (!next(0, 60000, cfg.workMs)) return 2; }
        else if (!strcmp(a, "--think"))          { if (!next(0, 60000, cfg.thinkMs)) return 2; }
        else if (!strcmp(a, "--holder-timeout")) { if (!next(1, 600000, cfg.holderTimeoutMs)) return 2; }
        else if (!strcmp(a, "--max-time"))       { if (!next(100, 3600000, cfg.maxTimeMs)) return 2; }
        else if (!strcmp(a, "--crash-at"))       { if (!next(1, 1000000, cfg.crashAt)) return 2; }
        else if (!strcmp(a, "--give-up"))        { if (!next(1, 1000000, cfg.giveUpBlocked)) return 2; }
        else if (!strcmp(a, "--run"))            { if (!next(0, 1000000, cfg.run)) return 2; }
        else if (!strcmp(a, "--role"))           { if (!next(0, 3, cfg.role)) return 2; }
        else if (!strcmp(a, "--ns"))             { if (!next(0, 2000000000, v)) return 2; cfg.ns = (DWORD)v; }
        else if (!strcmp(a, "--runs"))           { if (!next(1, 100, cfg.suiteRuns)) return 2; }
        else if (!strcmp(a, "--crash"))          cfg.crash = true;
        else if (!strcmp(a, "--no-sync"))        cfg.noSync = true;
        else if (!strcmp(a, "--no-recover"))     cfg.noRecover = true;
        else if (!strcmp(a, "--verbose"))        cfg.verbose = true;
        else if (!strcmp(a, "--suite") && i + 1 < argc)  cfg.suite = argv[++i];
        else if (!strcmp(a, "--csv") && i + 1 < argc)    cfg.csv = argv[++i];
        else if (!strcmp(a, "--config") && i + 1 < argc) cfg.config = argv[++i];
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { Usage(); return 0; }
        else { fprintf(stderr, "Unknown option: %s\n", a); Usage(); return 2; }
    }

    // Таймер 1 мс: иначе Sleep(1) на Windows по умолчанию округляется до ~15,6 мс,
    // что искажает моделирование коротких операций (используется в каждом процессе).
    timeBeginPeriod(1);
    int code;
    if (cfg.role != ROLE_PARENT)  code = RunParticipant(cfg);
    else if (!cfg.suite.empty())  code = RunSuite(cfg, cfg.suite);
    else                          code = RunParent(cfg);
    timeEndPeriod(1);
    return code;
}
