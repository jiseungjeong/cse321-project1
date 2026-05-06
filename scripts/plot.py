#!/usr/bin/env python3
"""Generate report figures from results/*.csv.

Inputs:  results/insert.csv  results/search.csv  results/range.csv
         results/delete.csv
Outputs: report/figures/*.png  (and a small console summary)

Only depends on matplotlib (no pandas/numpy beyond what matplotlib pulls in).
"""

from __future__ import annotations

import csv
import os
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")  # headless
import matplotlib.pyplot as plt

ROOT       = Path(__file__).resolve().parent.parent
RESULTS    = ROOT / "results"
# Figures live next to the LaTeX source so \includegraphics{figure/...}
# in report/project1/sample-sigconf.tex resolves directly.
FIGURES    = ROOT / "report" / "project1" / "figure"
FIGURES.mkdir(parents=True, exist_ok=True)

TREES = ["btree", "bstar", "bplus"]
TREE_LABEL = {"btree": "B-tree", "bstar": "B*-tree", "bplus": "B+-tree"}
COLORS = {"btree": "#1f77b4", "bstar": "#ff7f0e", "bplus": "#2ca02c"}


def read_csv(path: Path) -> list[dict]:
    with open(path) as f:
        return list(csv.DictReader(f))


def by_tree_d(rows: list[dict], y_field: str, cast=float) -> dict:
    out: dict = defaultdict(dict)
    for r in rows:
        out[r["kind"]][int(r["d"])] = cast(r[y_field])
    return out


def save_fig(fig, stem: str):
    """Save figure as both PNG (raster, fast preview) and PDF (vector,
    used by the LaTeX report). PDF avoids resampling artefacts when
    scaled in print."""
    fig.savefig(FIGURES / f"{stem}.png", dpi=150)
    fig.savefig(FIGURES / f"{stem}.pdf")


def grouped_bar(ax, data: dict, ds: list[int], title: str, ylabel: str):
    width = 0.25
    for i, kind in enumerate(TREES):
        ys = [data[kind][d] for d in ds]
        xs = [j + (i - 1) * width for j in range(len(ds))]
        ax.bar(xs, ys, width, label=TREE_LABEL[kind], color=COLORS[kind])
    ax.set_xticks(range(len(ds)))
    ax.set_xticklabels([f"d={d}" for d in ds])
    ax.set_title(title)
    ax.set_ylabel(ylabel)
    ax.grid(axis="y", linestyle=":", alpha=0.5)


# ------------------------ figures ------------------------

def fig_insert_time(rows: list[dict]):
    data = by_tree_d(rows, "insert_us")
    ds = sorted({int(r["d"]) for r in rows})
    fig, ax = plt.subplots(figsize=(5.5, 3.5))
    grouped_bar(ax, {k: {d: v / 1000.0 for d, v in vs.items()} for k, vs in data.items()},
                ds, "Insert 100K records", "wall time (ms)")
    ax.legend()
    fig.tight_layout()
    save_fig(fig, "insert_time")
    plt.close(fig)


def fig_insert_splits(rows: list[dict]):
    data = by_tree_d(rows, "splits", cast=int)
    ds = sorted({int(r["d"]) for r in rows})
    fig, ax = plt.subplots(figsize=(5.5, 3.5))
    grouped_bar(ax, {k: {d: int(v) for d, v in vs.items()} for k, vs in data.items()},
                ds, "Total splits during 100K insert",
                "splits (1-to-2 + 2-to-3)")
    ax.legend()
    fig.tight_layout()
    save_fig(fig, "insert_splits")
    plt.close(fig)


def fig_utilization(rows: list[dict]):
    util = by_tree_d(rows, "utilization")
    ds = sorted({int(r["d"]) for r in rows})
    fig, ax = plt.subplots(figsize=(5.5, 3.5))
    for kind in TREES:
        ys = [util[kind][d] for d in ds]
        ax.plot(ds, ys, "-o", label=TREE_LABEL[kind], color=COLORS[kind])
    ax.set_xticks(ds)
    ax.set_xlabel("order d")
    ax.set_ylabel("utilization (keys / capacity)")
    ax.set_title("Node utilization after full insert")
    ax.axhline(
        2/3, color="grey", linestyle=":", linewidth=0.8,
        label="2/3 theoretical mean (B-tree, random insert)",
    )
    ax.grid(linestyle=":", alpha=0.5)
    ax.legend()
    fig.tight_layout()
    save_fig(fig, "utilization")
    plt.close(fig)


def fig_search_time(rows: list[dict]):
    data = by_tree_d(rows, "avg_us")
    ds = sorted({int(r["d"]) for r in rows})
    fig, ax = plt.subplots(figsize=(5.5, 3.5))
    grouped_bar(ax, data, ds, "Point search (10K random keys)",
                "average time per search (µs)")
    ax.legend()
    fig.tight_layout()
    save_fig(fig, "search_time")
    plt.close(fig)


def fig_range_time(rows: list[dict]):
    data = by_tree_d(rows, "total_us")
    ds = sorted({int(r["d"]) for r in rows})
    fig, ax = plt.subplots(figsize=(5.5, 3.5))
    grouped_bar(ax, data, ds, "Range query [202000000, 202010000] — Male only",
                "total time (µs)")
    ax.legend()
    fig.tight_layout()
    save_fig(fig, "range_time")
    plt.close(fig)


def fig_range_sweep(rows: list[dict], suffix: str = ""):
    """Bonus: range query time vs window hit count."""
    by_kind: dict = defaultdict(list)
    for r in rows:
        by_kind[r["kind"]].append(r)

    fig, ax = plt.subplots(figsize=(5.5, 3.5))
    for kind in TREES:
        sub = sorted(by_kind[kind], key=lambda r: int(r["hits"]))
        xs = [int(r["hits"]) for r in sub]
        ys = [float(r["total_us"]) for r in sub]
        ax.plot(xs, ys, "-o", label=TREE_LABEL[kind], color=COLORS[kind])
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("hits in window (log)")
    ax.set_ylabel("range query time, µs (log)")
    title_suf = f" — d={rows[0]['d']}" if rows else ""
    ax.set_title("Range query time vs window size" + title_suf)
    ax.grid(which="both", linestyle=":", alpha=0.5)
    ax.legend()
    fig.tight_layout()
    save_fig(fig, f"range_sweep{suffix}")
    plt.close(fig)


def fig_fanout_sweep(ins_rows: list[dict], rng_rows: list[dict]):
    """Bonus: insert time and range query time vs fan-out d (log-log).

    Two-panel figure. Left panel: insert wall time vs d, with a 1/log(d)
    trend line fitted to the B-tree curve as a visual reference. Right
    panel: range query time on the fixed window (1403 hits) vs d. Both
    panels share the same x-axis (log-spaced d values).
    """
    import math

    by_kind_ins: dict = defaultdict(list)
    for r in ins_rows:
        by_kind_ins[r["kind"]].append(r)
    by_kind_rng: dict = defaultdict(list)
    for r in rng_rows:
        by_kind_rng[r["kind"]].append(r)

    # Vertically stacked so the figure fits a single column in the
    # two-column ACM layout.
    fig, axes = plt.subplots(2, 1, figsize=(4.0, 5.4))

    # collect x values once (same for both panels)
    btree_sub = sorted(by_kind_ins["btree"], key=lambda r: int(r["d"]))
    all_ds = [int(r["d"]) for r in btree_sub]

    ax = axes[0]
    for kind in TREES:
        sub = sorted(by_kind_ins[kind], key=lambda r: int(r["d"]))
        xs = [int(r["d"]) for r in sub]
        ys = [float(r["insert_us"]) / 1000.0 for r in sub]
        ax.plot(xs, ys, "-o", label=TREE_LABEL[kind], color=COLORS[kind])

    # 1/log(d) reference line, anchored to B-tree at d=3
    if btree_sub:
        anchor_x = int(btree_sub[0]["d"])
        anchor_y = float(btree_sub[0]["insert_us"]) / 1000.0
        c = anchor_y * math.log(anchor_x)
        ref = [c / math.log(x) for x in all_ds]
        ax.plot(all_ds, ref, "--", color="grey", linewidth=0.9,
                label=r"$1/\log d$ ref")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xticks(all_ds)
    ax.set_xticklabels([str(x) for x in all_ds], fontsize=6)
    ax.minorticks_off()
    ax.set_xlabel("order d (log)", fontsize=8)
    ax.set_ylabel("insert wall time, ms (log)", fontsize=8)
    ax.set_title("Insert 100K vs fan-out d", fontsize=9)
    ax.tick_params(axis="y", labelsize=7)
    ax.grid(which="both", linestyle=":", alpha=0.5)
    ax.legend(fontsize=7)

    ax = axes[1]
    for kind in TREES:
        sub = sorted(by_kind_rng[kind], key=lambda r: int(r["d"]))
        xs = [int(r["d"]) for r in sub]
        ys = [float(r["total_us"]) for r in sub]
        ax.plot(xs, ys, "-o", label=TREE_LABEL[kind], color=COLORS[kind])
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xticks(all_ds)
    ax.set_xticklabels([str(x) for x in all_ds], fontsize=6)
    ax.minorticks_off()
    ax.set_xlabel("order d (log)", fontsize=8)
    ax.set_ylabel("range query time, µs (log)", fontsize=8)
    ax.set_title("Range query (1403 hits) vs fan-out d", fontsize=9)
    ax.tick_params(axis="y", labelsize=7)
    ax.grid(which="both", linestyle=":", alpha=0.5)
    ax.legend(fontsize=7)

    fig.tight_layout()
    save_fig(fig, "fanout_sweep")
    plt.close(fig)


def fig_delete_breakdown(rows: list[dict]):
    # Group by (kind, d) and then by ratio. Three subplots: time, merges,
    # redistributes vs ratio for each (kind, d=5) — d=5 is the middle case.
    by_kind: dict = defaultdict(list)
    for r in rows:
        if int(r["d"]) != 5:
            continue
        by_kind[r["kind"]].append(r)

    # 3 vertically stacked panels so the figure fits a single column
    # in the two-column ACM layout.
    fig, axes = plt.subplots(3, 1, figsize=(4.0, 6.5), sharex=True)
    metrics = [("delete_us", "delete time (µs)", lambda v: float(v)),
               ("merges",    "merges",           lambda v: int(v)),
               ("redistributes", "redistributes", lambda v: int(v))]

    for ax, (col, ylabel, cast) in zip(axes, metrics):
        for kind in TREES:
            sub = sorted(by_kind[kind], key=lambda r: float(r["ratio"]))
            xs = [float(r["ratio"]) for r in sub]
            ys = [cast(r[col]) for r in sub]
            ax.plot(xs, ys, "-o", label=TREE_LABEL[kind], color=COLORS[kind])
        ax.set_ylabel(ylabel, fontsize=9)
        ax.grid(linestyle=":", alpha=0.5)
        ax.tick_params(labelsize=8)
    axes[0].set_title("d=5: delete time", fontsize=9)
    axes[1].set_title("d=5: merge count", fontsize=9)
    axes[2].set_title("d=5: redistribute count", fontsize=9)
    axes[2].set_xlabel("delete ratio", fontsize=9)
    axes[0].legend(loc="upper left", fontsize=7)
    fig.tight_layout()
    save_fig(fig, "delete_breakdown")
    plt.close(fig)


# ------------------------ console summary ------------------------

def print_summary():
    ins = read_csv(RESULTS / "insert.csv")
    rng = read_csv(RESULTS / "range.csv")

    print("\n=== Range query parity (cnt / male_hits / avg_gpa / avg_height) ===")
    parity = {(r["hits"], r["male_hits"], r["avg_gpa_male"], r["avg_height_male"])
              for r in rng}
    if len(parity) == 1:
        h, m, g, hh = parity.pop()
        print(f"  ALL TREES AGREE: {h} / {m} / {g} / {hh}")
    else:
        print(f"  WARNING: trees disagree, {len(parity)} distinct results")

    print("\n=== Insert summary ===")
    print(f"  {'tree':<7} {'d':>3} {'ms':>8} {'height':>7} {'splits':>9} {'redist':>9} {'util':>7}")
    for r in ins:
        print(f"  {r['kind']:<7} {r['d']:>3} {float(r['insert_us'])/1000:>8.1f} "
              f"{r['height']:>7} {r['splits']:>9} {r['redistributes']:>9} "
              f"{float(r['utilization']):>7.3f}")


def main():
    if not RESULTS.exists():
        print(f"missing {RESULTS}; run ./run all first", file=sys.stderr)
        sys.exit(1)

    ins = read_csv(RESULTS / "insert.csv")
    sea = read_csv(RESULTS / "search.csv")
    rng = read_csv(RESULTS / "range.csv")
    dele = read_csv(RESULTS / "delete.csv")

    fig_insert_time(ins)
    fig_insert_splits(ins)
    fig_utilization(ins)
    fig_search_time(sea)
    fig_range_time(rng)
    fig_delete_breakdown(dele)

    # Optional bonus figure(s)
    sweep_path = RESULTS / "range_sweep.csv"
    if sweep_path.exists():
        fig_range_sweep(read_csv(sweep_path), suffix="_d5")
    sweep_d3 = RESULTS / "range_sweep_d3.csv"
    if sweep_d3.exists():
        fig_range_sweep(read_csv(sweep_d3), suffix="_d3")

    fanout_ins = RESULTS / "sweep" / "insert.csv"
    fanout_rng = RESULTS / "sweep" / "range.csv"
    if fanout_ins.exists() and fanout_rng.exists():
        fig_fanout_sweep(read_csv(fanout_ins), read_csv(fanout_rng))

    print_summary()
    print(f"\nfigures written to {FIGURES}")


if __name__ == "__main__":
    main()
