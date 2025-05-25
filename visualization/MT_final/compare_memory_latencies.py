import os

import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np

from import_helper import *
plt.rcParams["xtick.labelsize"] = 20  # X tick label font size


COMPARISON_DIR = "tinymembench_pc_comparison"
MEMORY_BENCHMARK = "full_latency_no_volatile"
VERTICAL_LINES_CX28 = [(32000, "L1"), (512000, "L2"), (16000000, "L3")]
VERTICAL_LINES_NX05 = [(48000, "L1"), (1250000, "L2"), (48000000, "L3")]
TEXT_CX28 = [(2000, "L1"), (60000, "L2"), (1500000, "L3"), (20000000, "Memory")]
TEXT_NX05 = [(3000, "L1"), (120000, "L2"), (4000000, "L3")]
NODE = "nx06"


def single_views():
    tinymembench = import_benchmark(
        os.path.join(COMPARISON_DIR, "tinymembench_nx06.json")
    )
    pc_df = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/{MEMORY_BENCHMARK}")
    pc_df = pc_df[
        (pc_df["id"] == NODE)
        & (pc_df["config::run_on_node"] == 0)
        & (pc_df["config::alloc_on_node"] == 0)
        & (pc_df["config::madvise_huge_pages"] == True)
    ]
    pc = import_benchmark(os.path.join(COMPARISON_DIR, "pc.json"))

    benchmarks = {
        "Tinymembench": tinymembench,
        "Pointer Chasing": pc,
        "Both": [],
    }

    fig, axes = plt.subplots(1, 3, figsize=(10, 5), sharey=True, sharex=True)

    for ax, (title, benchmark) in zip(axes[:3], benchmarks.items()):
        if title == "Tinymembench":
            sizes = [entry["size"] for entry in benchmark["results"]]

            latencies = [
                entry["single_latency"] if entry["single_latency"] > 0.1 else 0.1
                for entry in benchmark["results"]
            ]
        else:
            sizes = pc_df["config::access_range"]
            latencies = pc_df["median_latency_single"].clip(lower=0.1)

        if title == "Tinymembench":
            sns.scatterplot(
                x=sizes,
                y=latencies,
                s=50,
                ax=ax,
                marker="P",
                color=COLORS[1],
            )
        else:
            sns.scatterplot(
                x=sizes,
                y=latencies,
                ax=ax,
                marker="x",
                s=30,
                linewidth=1.1,
                color=COLORS[0],
            )

        ax.set_title(title)
        ax.spines["top"].set_linewidth(1)
        ax.spines["top"].set_edgecolor("lightgray")
        ax.spines["top"].set_linestyle((0, (12, 12)))
        ax.spines["right"].set_linewidth(1)
        ax.spines["right"].set_edgecolor("lightgray")
        ax.spines["right"].set_linestyle((0, (12, 12)))
    # plot overlap

    tiny_sizes = [entry["size"] for entry in tinymembench["results"]]
    tiny_latencies = [
        entry["single_latency"] if entry["single_latency"] > 0.1 else 0.1
        for entry in tinymembench["results"]
    ]
    pc_sizes = pc_df["config::access_range"]
    pc_latencies = pc_df["median_latency_single"].clip(lower=0.1)

    sns.scatterplot(
        x=pc_sizes,
        y=pc_latencies,
        ax=axes[2],
        marker="x",
        color=COLORS[0],
        s=30,
        linewidth=1.1,
    )
    sns.scatterplot(
        x=tiny_sizes,
        y=tiny_latencies,
        ax=axes[2],
        marker="P",
        color=COLORS[1],
        s=50,
    )

    for ax in axes:
        if ax == axes[1]:
            ax.set_xlabel("Access Range [KB]", fontsize=20)
        else:
            ax.set_xlabel(" ")

        ax.set_xscale("log")
        ax.set_yscale("log")
        if ax == axes[0]:
            ax.set_ylabel("Latency [ns]", fontsize=20)
        ax.grid(axis="y")
        ax.set_yticks([0.1, 1, 10, 100], ["0.1", "1", "10", "100"], fontsize=18)
        for line_position, label in VERTICAL_LINES_NX05:
            ax.axvline(x=line_position, color=COLORS[3], linestyle="--")

        for text_position, label in TEXT_NX05:
            ax.text(
                text_position,
                ax.get_ylim()[0] + 0.1,
                label,
                color=COLORS[3],
                verticalalignment="bottom",
                size=14,
            )
    plt.tight_layout()
    plt.savefig(f"{MT_FINAL_FIGURES_DIR}/compare_memory_latencies.pdf")
    plt.show()

if __name__ == "__main__":
    single_views()
