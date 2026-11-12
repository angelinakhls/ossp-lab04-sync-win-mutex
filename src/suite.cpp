#include "suite.h"
#include "parent.h"
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace lab {
namespace {

// Корень проекта: ближайшая папка вверх от exe, где лежит CMakeLists.txt.
std::string ProjectRoot() {
    char p[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    std::string dir = p;
    for (int i = 0; i < 6; ++i) {
        size_t pos = dir.find_last_of("\\/");
        if (pos == std::string::npos) break;
        dir = dir.substr(0, pos);
        if (GetFileAttributesA((dir + "\\CMakeLists.txt").c_str()) != INVALID_FILE_ATTRIBUTES) return dir;
    }
    return ".";
}

void MakeDir(const std::string& d) { CreateDirectoryA(d.c_str(), nullptr); }

struct Case {
    std::string name;
    std::function<void(Config&)> setup;
    std::function<bool(const RunResult&)> check;
};

int Tests(const Config& base, const std::string& root) {
    MakeDir(root + "\\data");
    std::vector<Case> cases = {
        {"T1_timeout10",  [](Config& c) { c.timeoutMs = 10;  c.ops = 50; },
            [](const RunResult& r) { return r.violations == 0 && r.ok && r.abandoned == 0; }},
        {"T2_timeout100", [](Config& c) { c.timeoutMs = 100; c.ops = 50; },
            [](const RunResult& r) { return r.violations == 0 && r.ok && r.timeouts == 0; }},
        {"T3_crash_100",  [](Config& c) { c.timeoutMs = 100; c.ops = 50; c.crash = true; },
            [](const RunResult& r) { return r.violations == 0 && r.abandoned == 1 && r.recoveries == 1 && r.ok; }},
        {"T4_crash_10",   [](Config& c) { c.timeoutMs = 10;  c.ops = 50; c.crash = true; },
            [](const RunResult& r) { return r.violations == 0 && r.abandoned == 1 && r.recoveries == 1 && r.ok; }},
        {"T5_quarantine", [](Config& c) { c.timeoutMs = 100; c.ops = 50; c.crash = true; c.noRecover = true; },
            [](const RunResult& r) { return r.violations == 0 && r.blocked > 0 && r.ok; }},
        {"T6_nosync_must_fail", [](Config& c) { c.timeoutMs = 100; c.ops = 20; c.noSync = true; },
            [](const RunResult& r) { return r.violations > 0 && !r.ok; }},
        {"T7_watchdog",   [](Config& c) { c.timeoutMs = 100; c.ops = 100000; c.maxTimeMs = 300; },
            [](const RunResult& r) { return r.maxExceeded; }},
    };
    std::string csv = root + "\\data\\tests_runs.csv";
    DeleteFileA(csv.c_str());
    std::vector<std::string> lines;
    int failed = 0;
    for (auto& tc : cases) {
        Config c = base;
        c.suite.clear();
        c.config = tc.name; c.run = 1; c.csv = csv;
        tc.setup(c);
        RunResult r;
        RunParent(c, &r);
        bool ok = tc.check(r);
        std::string line = std::string(ok ? "PASS  " : "FAIL  ") + tc.name;
        printf("\n>>> %s\n", line.c_str());
        lines.push_back(line);
        if (!ok) ++failed;
    }
    lines.push_back("Failed checks: " + std::to_string(failed));
    printf("\n===== TEST SUMMARY =====\n");
    FILE* f = fopen((root + "\\data\\test_results.txt").c_str(), "w");
    for (auto& l : lines) { printf("%s\n", l.c_str()); if (f) fprintf(f, "%s\n", l.c_str()); }
    if (f) fclose(f);
    printf("Saved: %s\\data\\test_results.txt\n", root.c_str());
    return failed;
}

int Experiments(const Config& base, const std::string& root) {
    const int studentId = 114;
    const unsigned indSeed = 1000 + studentId * 97;      // 12058
    const int baseOps = 100, indOps = baseOps + studentId * 5;   // 670
    const unsigned stdSeed = 1;

    MakeDir(root + "\\data");
    MakeDir(root + "\\data\\logs");
    std::string csv = root + "\\data\\experiment_results.csv";
    DeleteFileA(csv.c_str());

    struct E { std::string name; int timeout; int ops; unsigned seed; bool crash, noRecover, noSync; };
    std::vector<E> ex = {
        {"A_std", 10, baseOps, stdSeed, false, false, false},
        {"B_std", 100, baseOps, stdSeed, false, false, false},
        {"C_std", 100, baseOps, stdSeed, true, false, false},
        {"A_ind", 10, indOps, indSeed, false, false, false},
        {"B_ind", 100, indOps, indSeed, false, false, false},
        {"C_ind", 100, indOps, indSeed, true, false, false},
    };
    for (int t : {5, 10, 20, 40, 50, 60, 100, 200})
        ex.push_back({"S_t" + std::to_string(t), t, baseOps, stdSeed, false, false, false});
    ex.push_back({"Q_norecover", 100, baseOps, stdSeed, true, true, false});
    ex.push_back({"N_nosync", 100, 20, stdSeed, false, false, true});

    int total = (int)ex.size() * base.suiteRuns, n = 0;
    for (auto& e : ex) {
        for (int run = 1; run <= base.suiteRuns; ++run) {
            Config c = base;
            c.suite.clear();
            c.config = e.name; c.run = run; c.csv = csv;
            c.timeoutMs = e.timeout; c.ops = e.ops; c.seed = e.seed;
            c.crash = e.crash; c.noRecover = e.noRecover; c.noSync = e.noSync;
            c.verbose = false;
            c.logFile = root + "\\data\\logs\\" + e.name + "_run" + std::to_string(run) + ".log";
            printf("\n>>> [%d/%d] %s run %d\n", ++n, total, e.name.c_str(), run);
            RunParent(c, nullptr);
        }
    }
    printf("\nDone. Results: %s\n", csv.c_str());
    return 0;
}

} // namespace

int RunSuite(const Config& base, const std::string& name) {
    std::string root = ProjectRoot();
    printf("Project root: %s\n", root.c_str());
    if (name == "tests") return Tests(base, root);
    if (name == "experiments") return Experiments(base, root);
    fprintf(stderr, "Unknown suite '%s' (use: tests | experiments)\n", name.c_str());
    return 2;
}

} // namespace lab
