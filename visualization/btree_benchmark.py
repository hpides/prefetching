import json
import os
import matplotlib.pyplot as plt
import seaborn as sns


from helper import *
import config


def plot_btree_benchmark():
    df = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/btree_benchmark_full_retain_true_sysstats"
    )
    print(df)

    unique_ids = df["id"].unique()
    unique_node_sizes = df["tree_node_size"].unique()
    unique_btree_variants = df["btree_variant"].unique()

    fig, axes = plt.subplots(
        len(unique_ids), 1, figsize=(12, 8 * len(unique_ids)), sharey=True, sharex=True
    )

    if len(unique_ids) == 1:
        axes = [axes]
    for i, (ax, unique_id) in enumerate(zip(axes, unique_ids)):
        df_id = df[df["id"] == unique_id]
        min_normal_runtime = df_id[df_id["btree_variant"] == "normal"][
            "lookup_runtime"
        ].min()
        print(f"id: {unique_id} min_runtime: {min_normal_runtime}")
        x = 0
        for btree_variant in unique_btree_variants:
            x_pos = []
            runtimes = []
            for tree_node_size in unique_node_sizes:
                min_runtime_index = df_id[
                    (df_id["btree_variant"] == btree_variant)
                    & (df_id["tree_node_size"] == tree_node_size)
                ]["lookup_runtime"].idxmin()
                best_btree_row = df_id.loc[min_runtime_index]
                best_btree_runtime = best_btree_row["lookup_runtime"]
                print(
                    f"{unique_id} - {btree_variant}:{tree_node_size}B: max throughput @ {best_btree_row['coroutines']}"
                )
                x_pos.append(x)
                runtimes.append(min_normal_runtime / best_btree_runtime)
                x += 1
            ax.bar(
                x_pos,
                runtimes,
                label=btree_variant,
                zorder=2,
                linewidth=1.0,
                edgecolor="black",
            )
            for tree_node_size, x_text in zip(unique_node_sizes, x_pos):
                ax.text(x_text - 0.1, 0.3, tree_node_size, rotation=90, zorder=3)
            ax.set_title(f"{unique_id}")
            if i == len(axes) - 1:
                ax.set_xlabel(
                    "Different Configurations (With best number of Coroutines)"
                )
            if i == int(len(axes) / 2):
                ax.set_ylabel("Speedup (over best baseline)")
            # ax.set_yscale("log")
            # ax.set_xscale("log")
            if i == 0:
                ax.legend(
                    loc="lower center",
                    bbox_to_anchor=(0.5, 1.2),
                    ncol=5,
                    fancybox=True,
                    shadow=True,
                )
            ax.grid(True)

    plt.subplots_adjust(hspace=0.3)  # Increase the space between subplots
    plt.show()


if __name__ == "__main__":
    plot_btree_benchmark()
