import json
import os
import matplotlib.pyplot as plt
import seaborn as sns


from helper import *


def plot_memory_latencies_heatmap():
    df = load_results_benchmark_directory_to_pandas(
        # f"{DATA_DIR}/full_latency_med_times_correct_allocation"
        f"{DATA_DIR}/full_latency_new_nodes"
    )
    df["config.access_range"] = df["config.access_range"].astype(int)
    df["config.alloc_on_node"] = df["config.alloc_on_node"].astype(int)
    df["config.run_on_node"] = df["config.run_on_node"].astype(int)
    df["config.madvise_huge_pages"] = df["config.madvise_huge_pages"].astype(bool)
    df["median_latency_single"] = df["median_latency_single"].astype(float)

    df = df[df["config.madvise_huge_pages"] == True]

    agg_df = (
        df.sort_values("config.access_range")
        .groupby(["id", "config.alloc_on_node", "config.run_on_node"])
        .last()
        .reset_index()
    )
    print(agg_df)
    for unique_id in agg_df["id"].unique():
        pivot_table = agg_df[agg_df["id"] == unique_id].pivot(
            index="config.run_on_node",
            columns="config.alloc_on_node",
            values="median_latency_single",
        )

        plt.figure(figsize=(10, 8))
        sns.heatmap(pivot_table, annot=True, fmt=".2f", cmap="YlGnBu")
        plt.title(f"Heatmap for ID: {unique_id}")
        plt.xlabel("config.alloc_on_node")
        plt.ylabel("config.run_on_node")
        plt.show()

def plot_memory_latencies():
    df = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/full_latency_med_times_correct_allocation"
    )
    # Ensure the data types are correct
    df["config.access_range"] = df["config.access_range"].astype(int)
    df["config.alloc_on_node"] = df["config.alloc_on_node"].astype(int)
    df["config.run_on_node"] = df["config.run_on_node"].astype(int)
    df["config.madvise_huge_pages"] = df["config.madvise_huge_pages"].astype(bool)
    df["median_latency_single"] = df["median_latency_single"].astype(float)

    # Get unique ids
    unique_ids = df["id"].unique()

    # Set up the matplotlib figure
    fig, axes = plt.subplots(len(unique_ids), 1, figsize=(12, 8 * len(unique_ids)))
    if len(unique_ids) == 1:
        axes = [axes]

    # Dictionary to store unique handles, labels, and colors
    unique_entries = {}
    colors = plt.get_cmap("tab10").colors  # Choose a colormap for distinct colors
    color_idx = 0

    # Loop through each unique id and plot
    for i, (ax, unique_id) in enumerate(zip(axes, unique_ids)):
        # Filter the DataFrame for the current id
        df_id = df[df["id"] == unique_id]

        # Group by 'config.alloc_on_node', 'config.run_on_node', and 'config.madvise_huge_pages'
        grouped = df_id.groupby(
            ["config.alloc_on_node", "config.run_on_node", "config.madvise_huge_pages"]
        )

        for (alloc_on_node, run_on_node, madvise_huge_pages), group in grouped:
            if alloc_on_node > run_on_node:
                continue

            # Create a label
            label = f"alloc_on_node={alloc_on_node}, run_on_node={run_on_node}, madvise_huge_pages={madvise_huge_pages}"

            # Check if the label is already added
            if label not in unique_entries:
                # Choose a color
                color = colors[color_idx % len(colors)]
                color_idx += 1

                # Plot 'config.access_range' vs 'latency_single'
                (line,) = ax.plot(
                    group.sort_values(by="config.access_range")["config.access_range"],
                    group.sort_values(by="config.access_range")[
                        "median_latency_single"
                    ],
                    marker="o",
                    color=color,
                    label=label,
                )
                unique_entries[label] = color

            else:
                # Plot with the existing color for the label
                color = unique_entries[label]
                ax.plot(
                    group.sort_values(by="config.access_range")["config.access_range"],
                    group.sort_values(by="config.access_range")[
                        "median_latency_single"
                    ],
                    marker="o",
                    color=color,
                )

        ax.set_title(f"Performance for {unique_id}")
        if i == len(unique_ids) - 1:
            ax.set_xlabel("Access Range")
        if i == len(unique_ids) // 2:
            ax.set_ylabel("Latency (Single)")
        ax.grid(True)
        ax.set_xscale("log")
        # ax.set_yscale("log")

    # Create a single legend for all subplots
    handles = [
        plt.Line2D([0], [0], color=color, marker="o")
        for color in unique_entries.values()
    ]
    labels = list(unique_entries.keys())
    fig.legend(
        handles,
        labels,
        loc="upper center",
        bbox_to_anchor=(0.5, 1.05),
        ncol=3,
        fontsize="small",
    )

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    plot_memory_latencies_heatmap()
    plot_memory_latencies()
