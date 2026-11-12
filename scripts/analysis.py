"""Обработка data/experiment_results.csv: агрегаты по конфигурациям (общий модуль)."""
import csv
import os
import statistics

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV_PATH = os.environ.get("LAB04_CSV", os.path.join(ROOT, "data", "experiment_results.csv"))

INT_FIELDS = ["run", "timeout_ms", "ops", "seed", "hold_ms", "crash", "no_sync", "no_recover",
              "elapsed_ms", "holder_holds", "w_success", "w_attempts", "w_timeouts", "w_abandoned",
              "recoveries", "blocked", "final_counter", "expected_counter", "violations",
              "invariants_ok", "max_time_exceeded"]
FLOAT_FIELDS = ["avg_wait_ms", "max_wait_ms"]


def load(path=CSV_PATH):
    """Возвращает список строк (словарей) с числовыми полями; пустой список, если файла нет."""
    if not os.path.exists(path):
        return []
    rows = []
    with open(path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            for k in INT_FIELDS:
                r[k] = int(r[k])
            for k in FLOAT_FIELDS:
                r[k] = float(r[k])
            rows.append(r)
    return rows


def by_config(rows):
    d = {}
    for r in rows:
        d.setdefault(r["config"], []).append(r)
    return d


def mean(values):
    return statistics.fmean(values) if values else float("nan")


def aggregate(runs):
    """Средние значения показателей по запускам одной конфигурации."""
    keys = ["elapsed_ms", "w_success", "w_attempts", "w_timeouts", "w_abandoned", "recoveries",
            "blocked", "avg_wait_ms", "max_wait_ms", "violations", "holder_holds"]
    agg = {k: mean([r[k] for r in runs]) for k in keys}
    agg["n"] = len(runs)
    agg["min_elapsed"] = min(r["elapsed_ms"] for r in runs)
    agg["max_elapsed"] = max(r["elapsed_ms"] for r in runs)
    agg["timeout_ms"] = runs[0]["timeout_ms"]
    agg["ops"] = runs[0]["ops"]
    agg["all_ok"] = all(r["invariants_ok"] == 1 for r in runs)
    return agg
