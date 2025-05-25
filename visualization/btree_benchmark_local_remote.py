import json
import os
import matplotlib.pyplot as plt
import seaborn as sns


from helper import *
from config import *

LATENCIES = {
    "AMD1": [153, 278],
    "AMD2": [82, 270],
    "ARM1": [151, 280],
    "ARM2": [180, 750],  # todo : verify
    "INTEL1": [80, 130],  # todo : verify
    "INTEL3": [84, 133],
}


def get_min_runtime(df, btree_variant):
    min_runtime_index = df[(df["btree_variant"] == btree_variant)][
        "lookup_runtime"
    ].idxmin()
    best_btree_row = df.loc[min_runtime_index]
    best_btree_runtime = best_btree_row["lookup_runtime"]
    return best_btree_runtime


def plot_btree_benchmark():
    df_local_mem = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/btree_benchmark_local_mem"
    )
    df_remote_mem = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/btree_benchmark_remote_mem"
    )

    unique_ids = df_local_mem["id"].unique()
    unique_node_sizes = df_local_mem["tree_node_size"].unique()
    unique_btree_variants = df_local_mem["btree_variant"].unique()
    unique_distributions = df_local_mem["key_distribution"].unique()

    fig, axes = plt.subplots(
        len(unique_distributions),
        len(unique_ids),
        figsize=(12, 8),
        sharey=True,
        sharex=True,
    )

    if len(unique_ids) == 1:
        axes = [axes]

    fig.suptitle(f"Local vs Remote BTree execution", fontsize=18)
    y = 0
    for dis in unique_distributions:
        x = 0
        for unique_id in sorted(unique_ids):
            df_local = df_local_mem[
                (df_local_mem["id"] == unique_id)
                & (df_local_mem["key_distribution"] == dis)
            ]
            df_remote = df_remote_mem[
                (df_remote_mem["id"] == unique_id)
                & (df_remote_mem["key_distribution"] == dis)
            ]
            min_normal_runtime_local = df_local[df_local["btree_variant"] == "normal"][
                "lookup_runtime"
            ].min()
            min_normal_runtime_remote = df_remote[
                df_remote["btree_variant"] == "normal"
            ]["lookup_runtime"].min()

            x_pos = 0
            for btree_variant in unique_btree_variants:
                if btree_variant == "normal":
                    continue
                best_btree_runtime_local = get_min_runtime(df_local, btree_variant)
                best_btree_runtime_remote = get_min_runtime(df_remote, btree_variant)

                speedups = [
                    min_normal_runtime_local / best_btree_runtime_local,
                    min_normal_runtime_remote / best_btree_runtime_remote,
                ]
                axes[y][x].bar(
                    [i + x_pos for i, _ in enumerate(speedups)],
                    speedups,
                    label=btree_variant,
                    zorder=2,
                    linewidth=1.0,
                    edgecolor="black",
                )
                x_pos += 2
            axes[y][x].set_title(f"{NODEID_TO_ARCH[unique_id]}")
            if x == int(len(axes[y]) / 2) and y == len(axes) - 1:
                axes[y][x].set_xlabel(
                    "Different Configurations (With best number of Coroutines)",
                    fontsize=16,
                )
            if x == 0:
                axes[y][x].set_ylabel("Speedup (over best baseline)", fontsize=16)
            # ax.set_yscale("log")
            # ax.set_xscale("log")
            if x == 3 and y == 0:
                axes[y][x].legend(
                    loc="lower center",
                    bbox_to_anchor=(0, 1.1),
                    ncol=5,
                    fancybox=True,
                    shadow=True,
                )
            axes[y][x].grid(True)

            x += 1
        y += 1
    plt.subplots_adjust(hspace=0.3)  # Increase the space between subplots
    plt.show()


def plot_latency_improvement():
    df_local_mem = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/btree_benchmark_local_mem"
    )
    df_remote_mem = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/btree_benchmark_remote_mem"
    )

    unique_ids = df_local_mem["id"].unique()
    unique_node_sizes = df_local_mem["tree_node_size"].unique()
    unique_btree_variants = df_local_mem["btree_variant"].unique()
    unique_distributions = df_local_mem["key_distribution"].unique()

    fig, axes = plt.subplots(
        1,
        1,
        figsize=(12, 8),
    )

    if len(unique_ids) == 1:
        axes = [axes]

    fig.suptitle(f"Latency change vs Throughput change", fontsize=18)
    y = 0
    for dis in unique_distributions:
        x = 0
        for unique_id in sorted(unique_ids):
            df_local = df_local_mem[
                (df_local_mem["id"] == unique_id)
                & (df_local_mem["key_distribution"] == dis)
            ]
            df_remote = df_remote_mem[
                (df_remote_mem["id"] == unique_id)
                & (df_remote_mem["key_distribution"] == dis)
            ]

            x_pos = 0
            for btree_variant in unique_btree_variants:

                # if btree_variant == "normal":
                #    continue
                best_btree_runtime_local = get_min_runtime(df_local, btree_variant)
                best_btree_runtime_remote = get_min_runtime(df_remote, btree_variant)

                runtimes = [
                    best_btree_runtime_local,
                    best_btree_runtime_remote,
                ]
                latencies = LATENCIES[unique_id]
                latencies = [lat / latencies[0] for lat in latencies]
                runtimes = [run / runtimes[0] for run in runtimes]
                axes.plot(
                    latencies,
                    runtimes,
                    "o",
                    color="black",
                    label=btree_variant,
                    zorder=2,
                )
                x_pos += 2
            axes.set_xlabel(
                "Latencies",
                fontsize=16,
            )
            axes.set_ylabel("Runtimes", fontsize=16)
            # ax.set_yscale("log")
            # ax.set_xscale("log")
            # axes.legend(
            #    loc="lower center",
            #    bbox_to_anchor=(0.5, 1.1),
            #    ncol=5,
            #    fancybox=True,
            #    shadow=True,
            # )
            axes.grid(True)

            x += 1
        y += 1
    plt.subplots_adjust(hspace=0.3)  # Increase the space between subplots
    plt.show()


if __name__ == "__main__":
    plot_btree_benchmark()
    plot_latency_improvement()
