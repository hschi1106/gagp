"""Optional standalone research figure from audited complete results (matplotlib)."""
import argparse
import hashlib
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path)
    args = parser.parse_args()
    result = json.loads(args.results.read_text())
    if not result["complete"]:
        raise RuntimeError("Figure requires complete, audited results")
    rows = result["rows"]
    matplotlib.rcParams.update({"pdf.fonttype": 42, "ps.fonttype": 42, "font.size": 10})
    height = max(5.0, len(rows)*0.37+2.0)
    fig, ax = plt.subplots(figsize=(9, height))
    y = np.arange(len(rows))
    rates = [100*r["successes"]/r["completed"] for r in rows]
    lower = [p-100*r["wilson95"][0] for p, r in zip(rates, rows)]
    upper = [100*r["wilson95"][1]-p for p, r in zip(rates, rows)]
    # Clamp harmless Wilson rounding at 0/1.
    lower, upper = np.maximum(lower, 0), np.maximum(upper, 0)
    ax.errorbar(rates, y, xerr=[lower, upper], fmt="o", color="#176f8a",
                capsize=3, label="Provided solver after C++ simplification (Wilson 95% CI)", zorder=3)
    ax.scatter([r["paper_successes_out_of_100"] for r in rows], y+0.13,
               marker="s", color="#b66c31", label="Short paper, Table III (/100)", zorder=3)
    dp = [(i, r) for i, r in enumerate(rows) if r["thesis_successes_out_of_25"] is not None]
    ax.scatter([4*r["thesis_successes_out_of_25"] for _, r in dp], [i-0.13 for i, _ in dp],
               marker="^", color="#795da3", label="Thesis DP, Table 5.4 (/25)", zorder=3)
    labels = [r["task"].replace("_", " ")+f"  ({r['successes']}/{r['completed']})" for r in rows]
    ax.set_yticks(y, labels)
    ax.invert_yaxis()
    ax.set_xlim(-3, 103)
    ax.set_xticks(range(0, 101, 20))
    ax.set_xlabel("Success rate (%)")
    ax.grid(axis="x", color="#dddddd", linewidth=0.7)
    ax.set_axisbelow(True)
    ax.spines[["top", "right", "left"]].set_visible(False)
    handles, labels = ax.get_legend_handles_labels()
    order = sorted(range(len(labels)), key=lambda i: not labels[i].startswith("Provided"))
    fig.legend([handles[i] for i in order], [labels[i] for i in order],
               loc="upper left", bbox_to_anchor=(0.29, 1-0.4/height), frameon=False, fontsize=9)
    fig.suptitle("ASGP reproduction with the supplied solver", y=0.99, fontsize=14)
    fig.text(0.02, 0.02, "Native schemes; P=1000; depth=7; NFE <= 500,000; source data and C++ simplifier.\n"
             "Original paper data/seeds and MATLAB simplification are unavailable; this is not exact replication.", fontsize=8)
    fig.subplots_adjust(left=0.29, right=0.97, bottom=1.1/height, top=1-1.5/height)
    out = args.results.parent
    for suffix in ("pdf", "png", "svg"):
        fig.savefig(out/f"success-rates.{suffix}", dpi=180, bbox_inches="tight")
    identity = {"measurements_sha256": hashlib.sha256(args.results.read_bytes()).hexdigest(),
                "plot_source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                "matplotlib": matplotlib.__version__, "numpy": np.__version__,
                "artifacts_sha256": {suffix: hashlib.sha256((out/f"success-rates.{suffix}").read_bytes()).hexdigest()
                                     for suffix in ("pdf", "png", "svg")}}
    (out/"figure-identity.json").write_text(json.dumps(identity, indent=2)+"\n")


if __name__ == "__main__":
    main()
