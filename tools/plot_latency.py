"""Графики для docs/Latency_Report.md.

    python tools/plot_latency.py [--samples docs/latency_samples.csv]
                                 [--summary docs/latency_summary.csv] [--out docs/graphs]

Метрики считает C++-утилита latency_report (docs/latency_summary.csv); здесь только
визуализация. Нужен matplotlib (pip install matplotlib).
"""
import argparse
import csv
import os
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

SERIES_ORDER = ["baseline", "delay_50", "delay_100", "jitter", "loss_5", "combined", "faults"]

# Палитра: категориальные слоты 1-2, статус critical для тайм-аутов, нейтральные чернила.
BLUE = "#2a78d6"
ORANGE = "#eb6834"
CRITICAL = "#d03b3b"
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_2 = "#52514e"
GRID = "#e4e3df"

plt.rcParams.update({
    "figure.facecolor": SURFACE,
    "axes.facecolor": SURFACE,
    "axes.edgecolor": GRID,
    "axes.labelcolor": INK_2,
    "axes.titlecolor": INK,
    "axes.titlesize": 11,
    "axes.labelsize": 9,
    "xtick.color": INK_2,
    "ytick.color": INK_2,
    "xtick.labelsize": 8,
    "ytick.labelsize": 8,
    "axes.grid": True,
    "grid.color": GRID,
    "grid.linewidth": 0.8,
    "axes.axisbelow": True,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "legend.frameon": False,
    "legend.fontsize": 9,
    "font.family": "DejaVu Sans",
})


def read_samples(path):
    rows = defaultdict(list)
    with open(path, encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f, delimiter=";"):
            if row["status"] not in ("received", "timeout"):
                continue
            rows[row["experiment_id"]].append({
                "sample": int(row["sample"]),
                "rtt": float(row["rtt_ms"]) if row["rtt_ms"] else None,
                "srtt": float(row["srtt_ms"]) if row["srtt_ms"] else None,
                "status": row["status"],
            })
    for series in rows.values():
        series.sort(key=lambda r: r["sample"])
    return rows


def read_summary(path):
    with open(path, encoding="utf-8", newline="") as f:
        return {row["experiment_id"]: row for row in csv.DictReader(f, delimiter=";")}


def ordered(ids):
    known = [s for s in SERIES_ORDER if s in ids]
    return known + sorted(s for s in ids if s not in SERIES_ORDER)


def plot_timeline(samples, out):
    ids = ordered(samples.keys())
    cols = 2
    rows_n = (len(ids) + cols - 1) // cols
    fig, axes = plt.subplots(rows_n, cols, figsize=(12, 2.6 * rows_n), squeeze=False)
    for ax, sid in zip(axes.flat, ids):
        data = samples[sid]
        xs = [r["sample"] for r in data if r["rtt"] is not None]
        rtt = [r["rtt"] for r in data if r["rtt"] is not None]
        srtt = [r["srtt"] for r in data if r["srtt"] is not None]
        ax.plot(xs, rtt, color=BLUE, linewidth=1.6, marker="o", markersize=3, label="RTT")
        ax.plot(xs, srtt, color=ORANGE, linewidth=2.0, label="SRTT")
        top = max(rtt) if rtt else 1.0
        lost = [r["sample"] for r in data if r["status"] == "timeout"]
        if lost:
            y = top * 1.12
            ax.scatter(lost, [y] * len(lost), marker="x", s=46, color=CRITICAL, linewidths=2,
                       label="тайм-аут", zorder=5)
            for x in lost:
                ax.axvline(x, color=CRITICAL, alpha=0.18, linewidth=1)
        ax.set_ylim(0, top * 1.25)
        ax.set_title(f"{sid}  (тайм-аутов: {len(lost)})", loc="left")
        ax.set_xlabel("номер измерения")
        ax.set_ylabel("мс")
    for ax in list(axes.flat)[len(ids):]:
        ax.set_visible(False)
    handles, labels = [], []
    for ax in axes.flat:
        for h, lab in zip(*ax.get_legend_handles_labels()):
            if lab not in labels:
                handles.append(h)
                labels.append(lab)
    fig.legend(handles, labels, loc="upper right", ncol=3)
    fig.suptitle("RTT по измерениям (масштаб оси Y у каждой серии свой)", x=0.01, ha="left",
                 fontsize=12, color=INK)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    fig.savefig(out, dpi=130)
    plt.close(fig)


def label_bars(ax, bars, fmt):
    for bar in bars:
        h = bar.get_height()
        ax.annotate(fmt.format(h), (bar.get_x() + bar.get_width() / 2, h), xytext=(0, 2),
                    textcoords="offset points", ha="center", va="bottom", fontsize=7, color=INK_2)


def plot_summary(summary, out):
    ids = ordered(summary.keys())
    mean = [float(summary[s]["mean_rtt_ms"]) for s in ids]
    srtt = [float(summary[s]["srtt_ms"]) for s in ids]
    loss = [float(summary[s]["loss_rate_pct"]) for s in ids]
    x = range(len(ids))
    width = 0.38

    # Две разные величины (мс и %) - две панели, а не две оси Y на одном графике.
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 4.2), gridspec_kw={"width_ratios": [3, 2]})
    b1 = ax1.bar([i - width / 2 - 0.01 for i in x], mean, width, color=BLUE, label="mean RTT")
    b2 = ax1.bar([i + width / 2 + 0.01 for i in x], srtt, width, color=ORANGE, label="SRTT (итог)")
    label_bars(ax1, b1, "{:.1f}")
    label_bars(ax1, b2, "{:.1f}")
    ax1.set_xticks(list(x), ids)
    ax1.set_ylabel("мс")
    ax1.set_title("Средний RTT и SRTT по сериям", loc="left")
    ax1.legend(loc="upper left")
    ax1.grid(axis="x", visible=False)

    b3 = ax2.bar(list(x), loss, 0.6, color=BLUE)
    label_bars(ax2, b3, "{:.0f} %")
    ax2.set_xticks(list(x), ids, rotation=30, ha="right")
    ax2.set_ylabel("%")
    ax2.set_ylim(0, max(10.0, max(loss) * 1.3))
    ax2.set_title("Loss rate = N_timeout / N_sent", loc="left")
    ax2.grid(axis="x", visible=False)
    for label in ax1.get_xticklabels():
        label.set_rotation(30)
        label.set_ha("right")
    fig.tight_layout()
    fig.savefig(out, dpi=130)
    plt.close(fig)


def plot_distribution(samples, out):
    ids = [s for s in ("baseline", "jitter", "combined") if s in samples]
    fig, axes = plt.subplots(2, len(ids), figsize=(4.2 * len(ids), 6.4), squeeze=False)
    for col, sid in enumerate(ids):
        rtt = [r["rtt"] for r in samples[sid] if r["rtt"] is not None]
        box_ax, hist_ax = axes[0][col], axes[1][col]
        box_ax.boxplot(rtt, orientation="horizontal", widths=0.5, showfliers=True,
                       boxprops={"color": BLUE, "linewidth": 1.6},
                       medianprops={"color": ORANGE, "linewidth": 2},
                       whiskerprops={"color": BLUE}, capprops={"color": BLUE},
                       flierprops={"marker": "o", "markersize": 4, "markerfacecolor": "none",
                                   "markeredgecolor": INK_2})
        box_ax.set_yticks([])
        box_ax.set_title(f"{sid}: box plot RTT (n={len(rtt)})", loc="left")
        box_ax.set_xlabel("мс")
        hist_ax.hist(rtt, bins=20, color=BLUE, edgecolor=SURFACE, linewidth=1.5)
        hist_ax.set_title(f"{sid}: гистограмма RTT", loc="left")
        hist_ax.set_xlabel("мс")
        hist_ax.set_ylabel("измерений")
        hist_ax.grid(axis="x", visible=False)
    fig.tight_layout()
    fig.savefig(out, dpi=130)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--samples", default="docs/latency_samples.csv")
    parser.add_argument("--summary", default="docs/latency_summary.csv")
    parser.add_argument("--out", default="docs/graphs")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)

    samples = read_samples(args.samples)
    summary = read_summary(args.summary)
    plot_timeline(samples, os.path.join(args.out, "rtt_timeline.png"))
    plot_summary(summary, os.path.join(args.out, "summary_bars.png"))
    plot_distribution(samples, os.path.join(args.out, "rtt_distribution.png"))
    print("Графики сохранены в", args.out)


if __name__ == "__main__":
    main()
