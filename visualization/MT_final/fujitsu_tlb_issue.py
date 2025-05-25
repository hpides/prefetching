import json
import os
import matplotlib.pyplot as plt
import seaborn as sns
from import_helper import *

plt.rcParams["xtick.labelsize"] = 22  # X tick label font size
plt.rcParams["ytick.labelsize"] = 22  # Y tick label font size


def get_pivots(df):
    node = "ARM1"

    df["config::access_range"] = df["config::access_range"].astype(int)
    df["config::alloc_on_node"] = df["config::alloc_on_node"].astype(int)
    df["config::run_on_node"] = df["config::run_on_node"].astype(int)
    df["config::madvise_huge_pages"] = df["config::madvise_huge_pages"].astype(bool)
    df["median_latency_single"] = df["median_latency_single"].astype(float)
    df["perf::dTLB-load-misses"] = df["perf::dTLB-load-misses"].fillna(-1).astype(float)

    # Filter the data
    df = df[
        (df["config::madvise_huge_pages"] == True)
        & (df["config::profile"] == True)
        & (df["id"] == node)
    ]

    agg_df = (
        df.sort_values("config::access_range")
        .groupby(["id", "config::alloc_on_node", "config::run_on_node"])
        .last()
        .reset_index()
    )
    pivot_table_lat = agg_df[agg_df["id"] == node].pivot(
        index="config::run_on_node",
        columns="config::alloc_on_node",
        values="median_latency_single",
    )

    pivot_table_tlb = agg_df[agg_df["id"] == node].pivot(
        index="config::run_on_node",
        columns="config::alloc_on_node",
        values="perf::dTLB-load-misses",
    )

    return [pivot_table_lat, pivot_table_tlb]


def plot_memory_latencies_heatmap():
    # Load the data
    df = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/numa_latencies_profiled"
    )
    df2 = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/numa_latencies_profiled_huge_alignment"
    )

    pivot_tables = get_pivots(df)
    pivot_tables2 = get_pivots(df2)
    n = 2

    fig, axes = plt.subplots(
        nrows=n,
        ncols=2,
        figsize=(10, 8),  # Adjust height depending on the number of heatmaps
    )

    # Flatten axes in case we have only 1 row of heatmaps
    axes = axes.flatten() if n > 1 else [axes]

    for offset, (pivot_table_lat, pivot_table_tlb) in enumerate(
        [pivot_tables, pivot_tables2]
    ):

        sns.heatmap(
            pivot_table_lat,
            annot=True,
            fmt=".0f",
            cmap=plt.cm.get_cmap("coolwarm"),
            ax=axes[0 + offset * 2],
            annot_kws={"size": 21},
        )
        sns.heatmap(
            pivot_table_tlb,
            annot=True,
            fmt=".2f",
            cmap=plt.cm.get_cmap("coolwarm"),
            ax=axes[1 + offset * 2],
            annot_kws={"size": 21},
        )

    axes[0].set_title(f"Measured Latencies", fontsize=29)
    axes[1].set_title(f"dTLB Miss Ratio", fontsize=29)

    fig.text(0.5, 0.04, "Allocate on Node", ha="center", va="center", fontsize=32)
    fig.text(
        0.04,
        0.5,
        "Run on Node",
        ha="center",
        va="center",
        rotation="vertical",
        fontsize=32,
    )

    for ax in axes:
        ax.set_xlabel("")
        ax.set_ylabel("")

    fig.line = fig.add_artist(
        plt.Line2D(
            [0.07, 0.95], [0.53, 0.53], color="black", linewidth=1, linestyle="dotted"
        )
    )

    fig.text(0.97, 0.75, "Run 1", ha="center", va="center", fontsize=29, rotation=90)
    fig.text(0.97, 0.29, "Run 2", ha="center", va="center", fontsize=29, rotation=90)

    # Adjust layout so titles and labels don't overlap
    plt.tight_layout(rect=[0.04, 0.07, 0.95, 1])  # Adjust margins for the text
    plt.subplots_adjust(hspace=0.25)
    # Show the final combined figure
    plt.show()
    fig.savefig(f"{MT_FINAL_FIGURES_DIR}/fujitsu_tlb_issue.pdf")


if __name__ == "__main__":
    plot_memory_latencies_heatmap()
