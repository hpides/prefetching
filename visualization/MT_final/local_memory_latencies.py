import json
import os
import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np

from import_helper import *

plt.rcParams["axes.labelsize"] = 27  # X and Y label font size
plt.rcParams["xtick.labelsize"] = 24  # X tick label font size
plt.rcParams["ytick.labelsize"] = 24  # Y tick label font size


MAPPINGS_AX = {
    "AMD1": 0,
    "AMD2": 0,
    "INTEL1": 1,
    "INTEL2": 1,
    "INTEL3": 1,
    "ARM1": 2,
    "ARM2": 2,
}
MAPPINGS_STYLE = {
    "AMD1": "dashed",
    "AMD2": "dashdot",
    "ARM1": "dashed",
    "ARM2": "dashdot",
    "INTEL1": "dashed",
    "INTEL2": "dashdot",
    "INTEL3": "dotted",
}

def plot_memory_latencies():
    df = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/full_latency_profile_huge_alignment"
    )
    # Ensure the data types are correct
    df["config::access_range"] = df["config::access_range"].astype(int)
    df["config::alloc_on_node"] = df["config::alloc_on_node"].astype(int)
    df["config::run_on_node"] = df["config::run_on_node"].astype(int)
    df["config::madvise_huge_pages"] = df["config::madvise_huge_pages"].astype(bool)
    df["median_latency_single"] = df["median_latency_single"].astype(float)

    # Get unique ids
    unique_ids = df["id"].unique()

    # Set up the matplotlib figure
    num_plots = len(unique_ids)
    columns = 3
    fig, axes = plt.subplots(
        1,
        columns,
        figsize=(14, 8),
        sharey=True,
        sharex=True,
    )

    if len(unique_ids) == 1:
        axes = [axes]
    else:
        axes = axes.ravel()
    # Dictionary to store unique handles, labels, and colors
    unique_entries = {}
    unique_ids = list(unique_ids)
    unique_ids.sort(key=lambda x: (int(x[-1]), x))
    # Loop through each unique id and plot
    for unique_id in unique_ids:
        i = MAPPINGS_AX[unique_id]
        ax = axes[i]
        ax.spines["top"].set_linewidth(1)
        ax.spines["top"].set_edgecolor("lightgray")
        ax.spines["top"].set_linestyle((0, (12, 12)))
        ax.spines["right"].set_linewidth(1)
        ax.spines["right"].set_edgecolor("lightgray")
        ax.spines["right"].set_linestyle((0, (12, 12)))
        df_id = df[
            (df["id"] == unique_id)
            & (df["config::alloc_on_node"] == 0)
            & (df["config::run_on_node"] == 0)
            & (df["config::madvise_huge_pages"] == True)
            & (df["config::profile"] == False)
        ]

        # Group by 'config::alloc_on_node', 'config::run_on_node', and 'config::madvise_huge_pages'

        sns.lineplot(
            x=df_id["config::access_range"] / 1024,
            y=df_id["median_latency_single"].clip(lower=0.1),
            ax=ax,
            label=NODEID_TO_CPU[unique_id],
            linewidth=2.5,
            linestyle=MAPPINGS_STYLE[unique_id],
        )

        ax.set_xscale("log")
        ax.set_yscale("log")
        if i % columns == 0:
            ax.set_ylabel("Latency [ns]")
        if i % columns == 1:
            ax.set_xlabel("Access Range [KB]")
        else:
            ax.set_xlabel("")

    for i in range(3):
        axes[i].grid(axis="y")
        axes[i].grid(axis="x")
    fig.suptitle("\n\n ")  # cheeky way to make some space for legends
    axes[0].set_yticks([0.1, 1, 5, 10, 50, 100], ["0.1", "1", "5", "10", "50", "100"])
    plt.tight_layout(rect=[0, 0, 1, 0.92])
    for i in range(3):
        axes[i].legend(
            loc="upper center",
            bbox_to_anchor=(0.5, 1.27),
            ncol=1,
            fancybox=True,
            shadow=False,
            fontsize=19,
        )
    plt.savefig(f"{MT_FINAL_FIGURES_DIR}/systems_local_memory_latencies.pdf")
    plt.show()


if __name__ == "__main__":
    plot_memory_latencies()
