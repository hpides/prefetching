import json
import os
import matplotlib.pyplot as plt
from matplotlib.ticker import FormatStrFormatter
from matplotlib.patches import Patch
import seaborn as sns
from matplotlib.patches import Rectangle
import numpy as np

from import_helper import *


plt.rcParams["xtick.labelsize"] = 20  # X tick label font size
plt.rcParams["ytick.labelsize"] = 20  # Y tick label font size
plt.rcParams["axes.labelsize"] = 18  # X and Y label font size
plt.rcParams["hatch.linewidth"] = 10
HATCH_WIDTH = 7
COLORS = list(sns.color_palette())
NODESIZE_COLOR = {512: COLORS[2], 8192: COLORS[1]}
COLOR_INDEX = 0
LABEL_FONT_SIZE = 23
plt.rcParams["hatch.linewidth"] = HATCH_WIDTH

RUNTIME_KEY = "lookup_runtime"
MADVISE_HUGEPAGE = True
FOLDER = "btree_benchmark_finale_merged"  # hash_join_64_prefetch_distance
NUM_COROUTINES = 20
NODE_SIZE = 8192
NODE_SIZES = [512, 8192]
BTREE_VARIANTS = ["normal", "coro_full_node", "coro_half_node"]
BTREE_VARIANTS_LABEL = {
    "normal": "Normal",
    "coro_full_node": "Coro_Full_Node",
    "coro_half_node": "Coro_Half_Node",
}

NODE_SIZES_HATCH = {512: "/", 8192: "\\"}
BTREE_VARIANTS_HATCH = {
    "normal": "/",
    "coro_full_node": "\\",
    "coro_half_node": "x",
}


def plot_btree_benchmark():
    global COLOR_INDEX
    Legend_set = False
    df = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/{FOLDER}")

    df = df[
        (df["config::madvise_huge_pages"] == MADVISE_HUGEPAGE)
        & (df["config::profile"] == False)
        & (df["config::coroutines"] == NUM_COROUTINES)
    ]

    unique_ids = UNIQUE_IDS
    unique_btree_variants = BTREE_VARIANTS

    number_columns = 2
    fig, axes = plt.subplots(2, 2, figsize=(8, 3), sharey=True, sharex=True)

    x_plot_offset = 0
    if len(unique_ids) == 1:
        axes = [axes]
    fig.subplots_adjust(left=0.09)
    fig.subplots_adjust(right=0.98)
    fig.subplots_adjust(bottom=0.32)
    fig.subplots_adjust(top=0.86)
    plt.subplots_adjust(wspace=0.0)
    plt.subplots_adjust(hspace=0.0)
    # for unique_id, ax_row in zip(unique_ids, axes):
    #    for data_placed_remote, ax in zip([False, True], ax_row):

    for data_placed_remote, ax_row in zip([False, True], axes):
        id_and_axes = [
            (
                (node_id, ax_row[0])
                if NODEID_TO_RELIABILITY[node_id] == "Strong"
                else (node_id, ax_row[1])
            )
            for node_id in unique_ids
        ]
        for unique_id, ax in id_and_axes:
            print(unique_id + (" Remote" if data_placed_remote else " Local"))
            df_id = df[
                (df["id"] == unique_id)
                & (df["config::run_remote_memory"] == data_placed_remote)
            ]
            baseline_runtimes = []
            coro_runtimes = []
            for node_size in NODE_SIZES:
                df_id_nodesize = df_id[(df_id["config::tree_node_size"] == node_size)]
                baseline_runtimes.append(
                    df_id_nodesize[
                        (df_id_nodesize["config::btree_variant"] == "normal")
                    ][RUNTIME_KEY].values[0]
                )
                coro_runtimes.append(
                    df_id_nodesize[
                        (df_id_nodesize["config::btree_variant"] == "coro_full_node")
                    ][RUNTIME_KEY].values[0]
                )
            speedups = [
                baseline / coro
                for baseline, coro in zip(baseline_runtimes, coro_runtimes)
            ]

            print(speedups)
            ax.spines["top"].set_visible(False)
            ax.spines["right"].set_visible(False)
            ax.spines["bottom"].set_visible(False)
            # ax.spines["left"].set_visible(False)
            # ax.spines["left"].set_linewidth(1)
            # ax.spines["left"].set_edgecolor("gray")
            # ax.spines["left"].set_linestyle((0, (12, 12)))
            for i, nodesize in enumerate(NODE_SIZES):
                bars = ax.scatter(
                    speedups[i],
                    i * 0.8,
                    label="_" if Legend_set else f"{nodesize} B Nodes",
                    zorder=3,
                    marker="x",
                    color=COLORS[i],
                )
            Legend_set = True
    for ax in axes.flatten():
        ax.tick_params(axis="both", which="both", length=0)
        ax.set_yticks([], [])
        ax.set_xticks([0.5, 1, 1.5, 2, 2.5, 3, 3.5], [0.5, 1, 1.5, 2, 2.5, 3, 3.5])
        ax.grid(axis="x")
        ax.set_ylim([-0.4, 1.2])
        ax.set_xlim([0.0, 4])
    axes[1][0].set_xlabel("Strong", fontsize=LABEL_FONT_SIZE - 2)
    axes[1][1].set_xlabel("Weak", fontsize=LABEL_FONT_SIZE - 2)
    axes[0][0].set_ylabel(" Local", fontsize=LABEL_FONT_SIZE - 2)
    axes[1][0].set_ylabel("Remote     ", fontsize=LABEL_FONT_SIZE - 2)
    fig.text(
        0.54,
        0.05,
        "Prefetching Reliability",
        ha="center",
        va="center",
        fontsize=LABEL_FONT_SIZE - 2,
    )
    fig.text(
        0.03,
        0.5,
        "  Data Placement",
        ha="center",
        va="center",
        rotation=90,
        fontsize=LABEL_FONT_SIZE - 2,
    )
    # fig.suptitle("Speedups using Prefetching on B+-Trees", fontsize=26, y=0.99)
    axes[0][1].legend(
        loc="lower center",
        bbox_to_anchor=(0, 0.78),
        ncol=4,
        frameon=False,
        # fancybox=True,
        shadow=False,
        fontsize=18,
    )
    # vertical line
    fig.add_artist(
        plt.Line2D(
            [0.535, 0.535],
            [0.12, 0.92],
            color="black",
            linewidth=1.1,
            # linestyle="dashed",
        )
    )
    fig.add_artist(
        plt.Line2D(
            [0.06, 0.95],
            [0.58, 0.58],
            color="black",
            linewidth=1.1,
            # linestyle="dashed",
        )
    )
    plt.savefig(f"{MT_FINAL_FIGURES_DIR}/paper_first_page_dots.pdf")
    plt.show()


if __name__ == "__main__":
    plot_btree_benchmark()
