"""Построение графиков по data/experiment_results.csv (только реальные измерения).
Запуск:  python scripts/plot_results.py        ->  report/figures/*.png
"""
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analysis  # noqa: E402

FIG_DIR = os.environ.get("LAB04_FIG", os.path.join(analysis.ROOT, "report", "figures"))


def sweep(groups):
    """Серия S_t<timeout>: список (timeout, агрегаты) по возрастанию тайм-аута."""
    pts = [(runs[0]["timeout_ms"], analysis.aggregate(runs))
           for name, runs in groups.items() if name.startswith("S_t")]
    return sorted(pts, key=lambda p: p[0])


def main():
    rows = analysis.load()
    if not rows:
        sys.exit("No data: run scripts/run_experiments.ps1 on Windows first")
    groups = analysis.by_config(rows)
    pts = sweep(groups)
    if not pts:
        sys.exit("No sweep configurations (S_t*) in data")
    os.makedirs(FIG_DIR, exist_ok=True)
    x = [p[0] for p in pts]

    # График 1: время выполнения от тайм-аута (среднее и разброс min..max)
    fig, ax = plt.subplots(figsize=(7, 4))
    avg = [p[1]["elapsed_ms"] for p in pts]
    lo = [a - p[1]["min_elapsed"] for a, p in zip(avg, pts)]
    hi = [p[1]["max_elapsed"] - a for a, p in zip(avg, pts)]
    ax.errorbar(x, avg, yerr=[lo, hi], marker="o", capsize=4)
    ax.set_xscale("log")
    ax.set_xticks(x)
    ax.set_xticklabels([str(v) for v in x])
    ax.set_xlabel("Тайм-аут ожидания Mutex, мс")
    ax.set_ylabel("Время выполнения, мс")
    ax.set_title("Время выполнения эксперимента от тайм-аута")
    ax.grid(True, alpha=0.4)
    fig.tight_layout()
    fig.savefig(os.path.join(FIG_DIR, "fig1_time_vs_timeout.png"), dpi=200)
    plt.close(fig)

    # График 2: число WAIT_TIMEOUT и попыток от тайм-аута
    fig, ax = plt.subplots(figsize=(7, 4))
    ax.plot(x, [p[1]["w_timeouts"] for p in pts], marker="s", label="WAIT_TIMEOUT")
    ax.plot(x, [p[1]["w_attempts"] for p in pts], marker="o", label="Все попытки Workers")
    ax.set_xscale("log")
    ax.set_xticks(x)
    ax.set_xticklabels([str(v) for v in x])
    ax.set_xlabel("Тайм-аут ожидания Mutex, мс")
    ax.set_ylabel("Количество, шт.")
    ax.set_title("Тайм-ауты и повторные попытки")
    ax.legend()
    ax.grid(True, alpha=0.4)
    fig.tight_layout()
    fig.savefig(os.path.join(FIG_DIR, "fig2_timeouts_vs_timeout.png"), dpi=200)
    plt.close(fig)

    # График 3: время ожидания (среднее и максимальное)
    fig, ax = plt.subplots(figsize=(7, 4))
    ax.plot(x, [p[1]["avg_wait_ms"] for p in pts], marker="o", label="Среднее ожидание за попытку")
    ax.plot(x, [p[1]["max_wait_ms"] for p in pts], marker="^", label="Максимальное ожидание")
    ax.set_xscale("log")
    ax.set_xticks(x)
    ax.set_xticklabels([str(v) for v in x])
    ax.set_xlabel("Тайм-аут ожидания Mutex, мс")
    ax.set_ylabel("Время ожидания, мс")
    ax.set_title("Время ожидания Mutex у Workers")
    ax.legend()
    ax.grid(True, alpha=0.4)
    fig.tight_layout()
    fig.savefig(os.path.join(FIG_DIR, "fig3_wait_vs_timeout.png"), dpi=200)
    plt.close(fig)

    # График 4: конфигурации А, Б, В (стандартные и индивидуальные параметры)
    names = [n for n in ["A_std", "B_std", "C_std", "A_ind", "B_ind", "C_ind"] if n in groups]
    if names:
        fig, ax = plt.subplots(figsize=(7, 4))
        vals = [analysis.aggregate(groups[n])["elapsed_ms"] for n in names]
        bars = ax.bar(names, vals)
        ax.bar_label(bars, fmt="%.0f")
        ax.set_ylabel("Среднее время выполнения, мс")
        ax.set_title("Конфигурации А, Б, В")
        ax.grid(True, axis="y", alpha=0.4)
        fig.tight_layout()
        fig.savefig(os.path.join(FIG_DIR, "fig4_configs.png"), dpi=200)
        plt.close(fig)
    print("Figures saved to", FIG_DIR)


if __name__ == "__main__":
    main()
