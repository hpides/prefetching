import json
import os
import matplotlib.pyplot as plt
import seaborn as sns


from helper import *
from config import *


VARIANT_COLOR = {}
COLOR_INDEX = 0


def plot_btree_benchmark():
    global COLOR_INDEX
    df = load_results_benchmark_directory_to_pandas(
        # f"{DATA_DIR}/btree_benchmark_zip_uniform"
        f"{DATA_DIR}/btree_benchmark_complete"
        # f"{DATA_DIR}/btree_benchmark_remote_mem"
    )
    print(df)

    unique_ids = df["id"].unique()
    unique_node_sizes = [512]  # df["tree_node_size"].unique()
    unique_btree_variants = df["btree_variant"].unique()
    unique_distributions = ["uniform"]  # df["key_distribution"].unique()
    # run_remote_memory = ["uniform"]  # df["key_distribution"].unique()

    for dis in unique_distributions:
        fig, axes = plt.subplots(
            1,
            len(unique_ids),
            figsize=(20, 8 * len(unique_ids)),
            sharey=True,
            sharex=True,
        )

        # fig.suptitle(f"{dis} Distribution", fontsize=18)
        if len(unique_ids) == 1:
            axes = [axes]
        for i, (ax, unique_id) in enumerate(zip(axes, unique_ids)):
            df_id = df[(df["id"] == unique_id) & (df["key_distribution"] == dis)]
            df_id_local = df_id[(df_id["run_remote_memory"] == False)]
            df_id_remote = df_id[(df_id["run_remote_memory"] == True)]
            min_normal_runtime = df_id[df_id["btree_variant"] == "normal"][
                "lookup_runtime"
            ].min()
            print(f"id: {unique_id} min_runtime: {min_normal_runtime}")
            x = 0
            for btree_variant in unique_btree_variants:
                if btree_variant == "coro_half_node_optimized":
                    continue
                if btree_variant not in VARIANT_COLOR:
                    VARIANT_COLOR[btree_variant] = COLORS[COLOR_INDEX]
                    COLOR_INDEX += 1
                x_pos = []
                runtimes = []
                for tree_node_size in unique_node_sizes:
                    runtimes = [
                        df_id_local[
                            (df_id_local["btree_variant"] == btree_variant)
                            & (df_id_local["tree_node_size"] == tree_node_size)
                            & (df_id_local["coroutines"] == 20)
                        ]["lookup_runtime"].values,
                        df_id_remote[
                            (df_id_remote["btree_variant"] == btree_variant)
                            & (df_id_remote["tree_node_size"] == tree_node_size)
                            & (df_id_remote["coroutines"] == 20)
                        ]["lookup_runtime"].values,
                    ]
                    runtimes = [float(runtime[0]) for runtime in runtimes]

                    x += 2
                print(x_pos)
                print(runtimes)
                ax.bar(
                    [i + x for i, _ in enumerate(runtimes)],
                    runtimes,
                    label=[f"{btree_variant} Local", f"{btree_variant} Remote"],
                    zorder=2,
                    linewidth=1.0,
                    hatch=["/", ""],
                    edgecolor="black",
                    color=VARIANT_COLOR[btree_variant],
                )
                for tree_node_size, x_text in zip(unique_node_sizes, x_pos):
                    ax.text(x_text - 0.1, 0.3, tree_node_size, rotation=90, zorder=3)
                ax.set_title(f"{NODEID_TO_ARCH[unique_id]}")
                if i == 1:
                    ax.set_xlabel(
                        "Different Configurations",
                        fontsize=16,
                    )
                if i == int(0):
                    ax.set_ylabel("Runtime", fontsize=16)
                # ax.set_yscale("log")
                # ax.set_xscale("log")
                if i == 1:
                    ax.legend(
                        loc="lower center",
                        bbox_to_anchor=(1, 1.05),
                        ncol=4,
                        fancybox=True,
                        shadow=True,
                    )
                ax.set_xticks([], [])

                ax.grid(True)

        plt.subplots_adjust(hspace=0.3)  # Increase the space between subplots
        plt.show()


def plot_btree_benchmar_speedup():
    df = load_results_benchmark_directory_to_pandas(
        # f"{DATA_DIR}/btree_benchmark_zip_uniform"
        f"{DATA_DIR}/btree_benchmark_complete"
        # f"{DATA_DIR}/btree_benchmark_remote_mem"
    )
    print(df)

    unique_ids = df["id"].unique()
    unique_node_sizes = [512]  # df["tree_node_size"].unique()
    unique_btree_variants = df["btree_variant"].unique()
    unique_distributions = ["uniform"]  # df["key_distribution"].unique()
    # run_remote_memory = ["uniform"]  # df["key_distribution"].unique()

    for dis in unique_distributions:
        fig, axes = plt.subplots(
            1,
            len(unique_ids),
            figsize=(20, 8 * len(unique_ids)),
            sharey=True,
            sharex=True,
        )

        # fig.suptitle(f"{dis} Distribution", fontsize=18)
        if len(unique_ids) == 1:
            axes = [axes]
        for i, (ax, unique_id) in enumerate(zip(axes, unique_ids)):
            df_id = df[(df["id"] == unique_id) & (df["key_distribution"] == dis)]
            df_id_local = df_id[(df_id["run_remote_memory"] == False)]
            df_id_remote = df_id[(df_id["run_remote_memory"] == True)]
            min_normal_runtime = df_id[df_id["btree_variant"] == "normal"][
                "lookup_runtime"
            ].min()
            print(f"id: {unique_id} min_runtime: {min_normal_runtime}")
            x = 0
            naive_runtimes = [
                df_id_local[
                    (df_id_local["btree_variant"] == "normal")
                    & (df_id_local["tree_node_size"] == 512)
                    & (df_id_local["coroutines"] == 20)
                ]["lookup_runtime"].values,
                df_id_remote[
                    (df_id_remote["btree_variant"] == "normal")
                    & (df_id_remote["tree_node_size"] == 512)
                    & (df_id_remote["coroutines"] == 20)
                ]["lookup_runtime"].values,
            ]
            for btree_variant in unique_btree_variants:
                if (
                    btree_variant == "normal"
                    or btree_variant == "coro_half_node_optimized"
                ):
                    continue
                x_pos = []
                runtimes = []
                for tree_node_size in unique_node_sizes:
                    runtimes = [
                        naive_runtimes[0]
                        / df_id_local[
                            (df_id_local["btree_variant"] == btree_variant)
                            & (df_id_local["tree_node_size"] == tree_node_size)
                            & (df_id_local["coroutines"] == 20)
                        ]["lookup_runtime"].values,
                        naive_runtimes[1]
                        / df_id_remote[
                            (df_id_remote["btree_variant"] == btree_variant)
                            & (df_id_remote["tree_node_size"] == tree_node_size)
                            & (df_id_remote["coroutines"] == 20)
                        ]["lookup_runtime"].values,
                    ]
                    runtimes = [float(runtime[0]) for runtime in runtimes]

                    x += 2
                print(x_pos)
                print(runtimes)
                ax.bar(
                    [i + x for i, _ in enumerate(runtimes)],
                    runtimes,
                    label=[f"{btree_variant} Local", f"{btree_variant} Remote"],
                    zorder=2,
                    linewidth=1.0,
                    hatch=["/", ""],
                    edgecolor="black",
                    color=VARIANT_COLOR[btree_variant],
                )
                for tree_node_size, x_text in zip(unique_node_sizes, x_pos):
                    ax.text(x_text - 0.1, 0.3, tree_node_size, rotation=90, zorder=3)
                ax.set_title(f"{NODEID_TO_ARCH[unique_id]}")
                if i == 1:
                    ax.set_xlabel(
                        "Different Configurations",
                        fontsize=16,
                    )
                if i == int(0):
                    ax.set_ylabel("Speedup", fontsize=16)
                ax.set_xticks([], [])
                # ax.set_yscale("log")
                # ax.set_xscale("log")
                if i == 1:
                    ax.legend(
                        loc="lower center",
                        bbox_to_anchor=(1, 1.05),
                        ncol=3,
                        fancybox=True,
                        shadow=True,
                    )
                ax.grid(True)

        plt.subplots_adjust(hspace=0.3)  # Increase the space between subplots
        plt.show()


if __name__ == "__main__":
    plot_btree_benchmark()
    plot_btree_benchmar_speedup()
