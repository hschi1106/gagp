"""Figures for the final report from analyze.py's results.json.

usage: charts.py RESULTS_JSON OUTDIR"""
import json, math, sys
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

SURFACE, INK, INK2, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"
S1, S2 = "#2a78d6", "#eb6834"   # reference categorical slots 1-2
plt.rcParams.update({"font.size": 10, "axes.edgecolor": GRID, "axes.labelcolor": INK2, "xtick.color": INK2,
                     "ytick.color": INK2, "axes.facecolor": SURFACE, "figure.facecolor": SURFACE,
                     "axes.spines.top": False, "axes.spines.right": False, "axes.grid": True,
                     "grid.color": GRID, "grid.linewidth": 0.8, "axes.axisbelow": True})


def speed_key(d): return "speed_vs_asgp_clean" if d.get("speed_vs_asgp_clean") else "speed_vs_asgp"


def fig_speed(d, out):
    sp = d[speed_key(d)]; pops = [p for p in d["pops"] if str(p) in sp]
    fig, ax = plt.subplots(figsize=(6, 3.6))
    geo = [sp[str(p)]["geo"] for p in pops]
    lo = [min(sp[str(p)]["per_task"].values()) for p in pops]; hi = [max(sp[str(p)]["per_task"].values()) for p in pops]
    ax.fill_between(pops, lo, hi, color=S1, alpha=0.12, linewidth=0)
    ax.plot(pops, geo, color=S1, linewidth=2, marker="o", markersize=8, markeredgecolor=SURFACE, markeredgewidth=2)
    for p, g in zip(pops, geo): ax.annotate(f"{g:.0f}x", (p, g), textcoords="offset points", xytext=(0, 9), ha="center", color=INK)
    ax.annotate("task range (min-max)", (pops[-1], hi[-1]), textcoords="offset points", xytext=(-4, -12), ha="right", color=INK2, fontsize=9)
    ax.set_xscale("log", base=2); ax.set_yscale("log"); ax.set_xticks(pops, [str(p) for p in pops]); ax.minorticks_off()
    ax.set_xlabel("population size P"); ax.set_ylabel("ASGP-1T time / GAGP time")
    ax.set_title("GAGP speedup over ASGP-1T (geo mean over tasks)", loc="left", color=INK, fontsize=11)
    fig.tight_layout(); fig.savefig(out / "fig_speedup.png", dpi=160); plt.close(fig)


def fig_time(d, out):
    sp = d[speed_key(d)]; pops = [p for p in d["pops"] if str(p) in sp]
    fig, ax = plt.subplots(figsize=(6, 3.6))
    for key, label, col in (("asgp_s", "ASGP-1T", S2), ("gagp_s", "GAGP GPU", S1)):
        ys = [sp[str(p)][key] / sp[str(p)]["cells"] / 60 for p in pops]
        ax.plot(pops, ys, color=col, linewidth=2, marker="o", markersize=8, markeredgecolor=SURFACE, markeredgewidth=2, label=label)
        ax.annotate(label, (pops[-1], ys[-1]), textcoords="offset points", xytext=(8, -3), color=INK)
    ax.set_xscale("log", base=2); ax.set_yscale("log"); ax.set_xticks(pops, [str(p) for p in pops]); ax.minorticks_off()
    ax.set_xlim(pops[0] / 1.4, pops[-1] * 2.6)
    ax.set_xlabel("population size P"); ax.set_ylabel("mean search time per run (min)")
    ax.set_title("Search time per run, 64 generations, 1024 cases", loc="left", color=INK, fontsize=11)
    ax.legend(frameon=False, loc="upper left"); fig.tight_layout(); fig.savefig(out / "fig_time.png", dpi=160); plt.close(fig)


def fig_quality(d, out):
    q = d["quality"]["test"]; pops = [p for p in d["pops"] if str(p) in q]
    series = [("GAGP (steady, B=P)", S1, [q[str(p)]["gagp"] / q[str(p)]["pairs"] for p in pops]),
              ("ASGP-1T", S2, [q[str(p)]["asgp"] / q[str(p)]["pairs"] for p in pops])]
    fig, ax = plt.subplots(figsize=(6, 3.6)); w = 0.8 / len(series)
    for i, (label, col, ys) in enumerate(series):
        xs = [j + (i - (len(series) - 1) / 2) * w for j in range(len(pops))]
        ax.bar(xs, [100 * y for y in ys], width=w - 0.03, color=col, label=label, edgecolor=SURFACE, linewidth=2)
        for x, y in zip(xs, ys): ax.annotate(f"{100 * y:.0f}", (x, 100 * y), textcoords="offset points", xytext=(0, 3), ha="center", color=INK2, fontsize=8)
    ax.set_xticks(range(len(pops)), [f"P{p}" for p in pops]); ax.set_ylabel("runs solving all test cases (%)")
    ax.set_title("Held-out test solve rate (paired runs)", loc="left", color=INK, fontsize=11)
    ax.grid(axis="x", visible=False); ax.legend(frameon=False, loc="upper left", fontsize=9)
    fig.tight_layout(); fig.savefig(out / "fig_quality.png", dpi=160); plt.close(fig)


if __name__ == "__main__":
    d = json.loads(Path(sys.argv[1]).read_text()); out = Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
    fig_speed(d, out); fig_time(d, out); fig_quality(d, out)
