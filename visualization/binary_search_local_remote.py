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

VARIANT_COLOR = {}
COLOR_INDEX = 0


def get_min_runtime(df, btree_variant):
    min_runtime_index = df[(df["btree_variant"] == btree_variant)][
        "lookup_runtime"
    ].idxmin()
    best_btree_row = df.loc[min_runtime_index]
    best_btree_runtime = best_btree_row["lookup_runtime"]
    return best_btree_runtime


def plot_btree_benchmark():
    global COLOR_INDEX
    df = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/binary_search_remote")

    unique_ids = df["id"].unique()
    unique_variants = df["binary_search_variant"].unique()
    unique_distributions = ["uniform"]  # df["key_distribution"].unique()

    fig, axes = plt.subplots(
        len(unique_distributions),
        len(unique_ids),
        figsize=(20, 8),
        sharey=True,
        sharex=True,
    )

    if len(unique_ids) == 1 or len(unique_distributions) == 1:
        axes = [axes]
    # if :
    #    axes = [[ax] for ax in axes]
    print(axes)
    fig.suptitle(f"Local vs Remote Binary Search execution (uniform)", fontsize=18)
    y = 0
    for dis in unique_distributions:
        x = 0
        for unique_id in sorted(unique_ids):
            df_local = df[
                (df["id"] == unique_id)
                & (df["key_distribution"] == dis)
                & (df["run_remote_memory"] == False)
                & (df["parallel_streams"] == 20)
                & (df["num_elements"] == 100000000)
            ]
            df_remote = df[
                (df["id"] == unique_id)
                & (df["key_distribution"] == dis)
                & (df["run_remote_memory"] == True)
                & (df["parallel_streams"] == 20)
                & (df["num_elements"] == 100000000)
            ]

            x_pos = 0
            for bs_variant in unique_variants:
                if bs_variant not in VARIANT_COLOR:
                    VARIANT_COLOR[bs_variant] = COLORS[COLOR_INDEX]
                    COLOR_INDEX += 1
                runtimes = [
                    df_local[df_local["binary_search_variant"] == bs_variant][
                        "lookup_runtime"
                    ],
                    df_remote[df_remote["binary_search_variant"] == bs_variant][
                        "lookup_runtime"
                    ],
                ]
                runtimes = [float(runtime) for runtime in runtimes]
                print(runtimes)
                axes[y][x].bar(
                    [i + x_pos for i, _ in enumerate(runtimes)],
                    runtimes,
                    label=[f"{bs_variant} Local", f"{bs_variant} Remote"],
                    zorder=2,
                    linewidth=1.0,
                    edgecolor="black",
                    hatch=["/", ""],
                    color=VARIANT_COLOR[bs_variant],
                )
                x_pos += 2
            axes[y][x].set_title(f"{NODEID_TO_ARCH[unique_id]}")
            axes[y][x].set_xticks([], [])

            if x == int(len(axes[y]) / 2) and y == len(axes) - 1:
                axes[y][x].set_xlabel(
                    "Configs",
                    fontsize=16,
                )
            if x == 0:
                axes[y][x].set_ylabel("Lookup runtime", fontsize=16)
            # ax.set_yscale("log")
            # ax.set_xscale("log")
            if x == 3 and y == 0:
                axes[y][x].legend(
                    loc="lower center",
                    bbox_to_anchor=(0, 1.05),
                    ncol=6,
                    fancybox=True,
                    shadow=True,
                )
            axes[y][x].grid(True)

            x += 1
        y += 1
    plt.subplots_adjust(hspace=0.3)  # Increase the space between subplots
    plt.show()


def plot_btree_benchmark_speedup():
    df = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/binary_search_remote")

    unique_ids = df["id"].unique()
    unique_variants = df["binary_search_variant"].unique()
    unique_distributions = ["uniform"]  # df["key_distribution"].unique()

    fig, axes = plt.subplots(
        len(unique_distributions),
        len(unique_ids),
        figsize=(20, 8),
        sharey=True,
        sharex=True,
    )

    if len(unique_ids) == 1 or len(unique_distributions) == 1:
        axes = [axes]
    # if :
    #    axes = [[ax] for ax in axes]
    print(axes)
    fig.suptitle(f"Local vs Remote Binary Search execution (uniform)", fontsize=18)
    y = 0
    for dis in unique_distributions:
        x = 0
        for unique_id in sorted(unique_ids):
            df_local = df[
                (df["id"] == unique_id)
                & (df["key_distribution"] == dis)
                & (df["run_remote_memory"] == False)
                & (df["parallel_streams"] == 20)
                & (df["num_elements"] == 100000000)
            ]
            df_remote = df[
                (df["id"] == unique_id)
                & (df["key_distribution"] == dis)
                & (df["run_remote_memory"] == True)
                & (df["parallel_streams"] == 20)
                & (df["num_elements"] == 100000000)
            ]

            naive_runtimes = [
                df_local[df_local["binary_search_variant"] == "naive"][
                    "lookup_runtime"
                ],
                df_remote[df_remote["binary_search_variant"] == "naive"][
                    "lookup_runtime"
                ],
            ]
            x_pos = 0
            for bs_variant in unique_variants:
                if bs_variant == "naive":
                    continue
                speedups = [
                    naive_runtimes[0]
                    / df_local[df_local["binary_search_variant"] == bs_variant][
                        "lookup_runtime"
                    ].values,
                    naive_runtimes[1]
                    / df_remote[df_remote["binary_search_variant"] == bs_variant][
                        "lookup_runtime"
                    ].values,
                ]
                print(speedups)
                speedups = [float(speedup) for speedup in speedups]
                print(speedups)
                axes[y][x].bar(
                    [i + x_pos for i, _ in enumerate(speedups)],
                    speedups,
                    label=[f"{bs_variant} Local", f"{bs_variant} Remote"],
                    zorder=2,
                    linewidth=1.0,
                    edgecolor="black",
                    hatch=["/", ""],
                    color=VARIANT_COLOR[bs_variant],
                )
                x_pos += 2
            axes[y][x].set_title(f"{NODEID_TO_ARCH[unique_id]}")
            if x == int(len(axes[y]) / 2) and y == len(axes) - 1:
                axes[y][x].set_xlabel(
                    "Configs",
                    fontsize=16,
                )
            if x == 0:
                axes[y][x].set_ylabel("Speedup vs baseline", fontsize=16)
            # ax.set_yscale("log")
            # ax.set_xscale("log")
            if x == 3 and y == 0:
                axes[y][x].legend(
                    loc="lower center",
                    bbox_to_anchor=(0, 1.05),
                    ncol=6,
                    fancybox=True,
                    shadow=True,
                )
            axes[y][x].grid(True)
            axes[y][x].set_xticks([], [])
            x += 1
        y += 1
    plt.subplots_adjust(hspace=0.3)  # Increase the space between subplots
    plt.show()


if __name__ == "__main__":
    plot_btree_benchmark()
    plot_btree_benchmark_speedup()
