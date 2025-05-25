import json
import os
import matplotlib.pyplot as plt
import seaborn as sns
from import_helper import *

plt.rcParams["xtick.labelsize"] = 18  # X tick label font size
plt.rcParams["ytick.labelsize"] = 18  # Y tick label font size

MAPPINGS_AX = {
    "AMD1": 0,
    "AMD2": 3,
    "INTEL1": 1,
    "INTEL2": 4,
    "INTEL3": 7,
    "ARM1": 2,
    "ARM2": 5,
}

EMPTY_AXES = [6, 8]


def plot_memory_latencies_heatmap():
    # Load the data
    df = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/numa_latencies_profiled_huge_alignment"
    )
    df["config::access_range"] = df["config::access_range"].astype(int)
    df["config::alloc_on_node"] = df["config::alloc_on_node"].astype(int)
    df["config::run_on_node"] = df["config::run_on_node"].astype(int)
    df["config::madvise_huge_pages"] = df["config::madvise_huge_pages"].astype(bool)
    df["median_latency_single"] = df["median_latency_single"].astype(float)

    # Filter the data
    df = df[df["config::profile"] == False]

    # Aggregate the data
    agg_df = (
        df.sort_values("config::access_range")
        .groupby(
            [
                "id",
                "config::alloc_on_node",
                "config::run_on_node",
                "config::madvise_huge_pages",
            ]
        )
        .last()
        .reset_index()
    )

    print(agg_df)

    # Get unique IDs to determine how many heatmaps we need
    unique_ids = agg_df["id"].unique()
    n_ids = len(unique_ids)

    # Create a grid of subplots based on the number of unique IDs
    fig, axes = plt.subplots(
        nrows=(n_ids // 3) + (n_ids % 3),  # 2 columns, adjust rows accordingly
        ncols=3,
        figsize=(12, 10),  # Adjust height depending on the number of heatmaps
    )

    # Flatten axes in case we have only 1 row of heatmaps
    axes = axes.flatten() if n_ids > 1 else [axes]

    for unique_id in unique_ids:
        i = MAPPINGS_AX[unique_id]
        id_df = agg_df[(agg_df["id"] == unique_id)]
        print(id_df)

        if unique_id == "ARM1":
            id_df = id_df[(id_df["config::madvise_huge_pages"] == False)]
        else:
            id_df = id_df[(id_df["config::madvise_huge_pages"] == True)]

        print(id_df)
        # Create a pivot table for the current ID
        pivot_table = id_df.pivot(
            index="config::run_on_node",
            columns="config::alloc_on_node",
            values="median_latency_single",
        )

        # Fill missing values by copying the lower triangle to the upper triangle
        # pivot_table = pivot_table.combine_first(pivot_table.T)

        # Plot the heatmap on the corresponding subplot
        sns.heatmap(
            pivot_table,
            annot=True,
            fmt=".0f" if unique_id == "AMD2" else ".0f",
            cmap=plt.cm.get_cmap("coolwarm"),
            ax=axes[i],
            annot_kws={"size": 16 if unique_id == "AMD2" else 23},
        )
        axes[i].set_yticklabels(axes[i].get_yticklabels(), rotation=0, fontsize=21)
        axes[i].set_xticklabels(axes[i].get_xticklabels(), rotation=0, fontsize=21)
        axes[i].set_title(f"{unique_id}", fontsize=26)
        axes[i].set_xlabel("")
        axes[i].set_ylabel("")
    for ax in EMPTY_AXES:
        axes[ax].axis("off")
    fig.text(0.48, 0.03, "Allocate on Node", ha="center", va="center", fontsize=28)
    fig.text(
        0.32,
        0.21,
        "Run on Node",
        ha="center",
        va="center",
        rotation="vertical",
        fontsize=28,
    )
    fig.text(
        0.67,
        0.21,
        "Latency [ns]",
        ha="center",
        va="center",
        rotation="vertical",
        fontsize=28,
    )

    # Adjust layout so titles and labels don't overlap
    plt.tight_layout(rect=[0, 0.04, 1, 1])  # Adjust margins for the text
    plt.subplots_adjust(wspace=0.15, hspace=0.33, top=0.94)
    # Show the final combined figure
    plt.show()
    fig.savefig(f"{MT_FINAL_FIGURES_DIR}/numa_heatmaps.pdf")


if __name__ == "__main__":
    plot_memory_latencies_heatmap()
