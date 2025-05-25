import json
import os
import matplotlib.pyplot as plt
from matplotlib.ticker import FormatStrFormatter
from matplotlib.patches import Patch
import seaborn as sns
from matplotlib.patches import Rectangle
import numpy as np

from import_helper import *


plt.rcParams["xtick.labelsize"] = 24  # X tick label font size
plt.rcParams["ytick.labelsize"] = 24  # Y tick label font size
plt.rcParams["axes.labelsize"] = 18  # X and Y label font size
plt.rcParams["hatch.linewidth"] = 10
HATCH_WIDTH = 7
VARIANT_COLOR = {}
COLOR_INDEX = 0
LABEL_FONT_SIZE = 28
plt.rcParams["hatch.linewidth"] = HATCH_WIDTH

RUNTIME_KEY = "lookup_runtime"
MADVISE_HUGEPAGE = True
FOLDER = "btree_benchmark_finale_merged"  # hash_join_64_prefetch_distance
ARM_FOLDER = "btree_benchmark_finale_huge_nodes"  # hash_join_64_prefetch_distance
NUM_COROUTINES = 20
NUM_ARM_COROUTINES = 8
NODE_SIZE = 8192
NODE_SIZE_ARM = 32768
BTREE_VARIANTS = ["normal", "coro_full_node", "coro_half_node"]
BTREE_VARIANTS_LABEL = {
    "normal": "Normal",
    "coro_full_node": "Coro_Full_Node",
    "coro_half_node": "Coro_Half_Node",
}

BTREE_VARIANTS_HATCH = {
    "normal": "/",
    "coro_full_node": "\\",
    "coro_half_node": "x",
}


def plot_btree_benchmark():
    global COLOR_INDEX
    df = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/{FOLDER}")
    df_arm1 = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/{ARM_FOLDER}")

    df = df[
        (df["config::madvise_huge_pages"] == MADVISE_HUGEPAGE)
        & (df["config::profile"] == False)
        & (df["config::coroutines"] == NUM_COROUTINES)
        & (df["config::tree_node_size"] == NODE_SIZE)
    ]
    df_arm1 = df_arm1[
        (df_arm1["config::madvise_huge_pages"] == MADVISE_HUGEPAGE)
        & (df_arm1["config::profile"] == False)
        & (df_arm1["config::coroutines"] == NUM_ARM_COROUTINES)
        & (df_arm1["config::tree_node_size"] == NODE_SIZE_ARM)
    ]
    unique_ids = df["id"].unique()
    unique_btree_variants = BTREE_VARIANTS

    number_columns = len(unique_ids) + 1
    fig, axes = plt.subplots(
        2,
        number_columns,
        figsize=(17, 8.5),
    )

    x_plot_offset = 0
    # fig.suptitle(f"{dis} Distribution", fontsize=18)
    if len(unique_ids) == 1:
        axes = [axes]
    fig.subplots_adjust(left=0.08)
    fig.subplots_adjust(bottom=0.15)
    fig.subplots_adjust(top=0.91)
    plt.subplots_adjust(wspace=0.02)
    plt.subplots_adjust(hspace=0.1)
    axes = axes.flatten()
    node_groups_filtered = [g for g in GROUPS_FLATTENED if g[0] in unique_ids]
    i = -1
    for reliability, (unique_id, group_id) in [
        (rel, node_group_filtered)
        for node_group_filtered in node_groups_filtered
        for rel in [True, False]
    ]:
        if unique_id == "ARM1":
            df_id = df_arm1[
                (df_arm1["id"] == unique_id)
                & (df_arm1["config::reliability"] == reliability)
            ]
        else:
            df_id = df[
                (df["id"] == unique_id) & (df["config::reliability"] == reliability)
            ]

        if len(df_id) == 0:
            continue
        i += 1
        ax = axes[i]
        ax.yaxis.set_major_formatter(FormatStrFormatter("%.1f"))
        df_id_local = df_id[(df_id["config::run_remote_memory"] == False)]
        df_id_remote = df_id[(df_id["config::run_remote_memory"] == True)]
        x_positions = [0, 3.5, 1.13, 4.63, 2.26, 5.76]
        x = -2
        for btree_variant in unique_btree_variants:
            if btree_variant not in VARIANT_COLOR:
                VARIANT_COLOR[btree_variant] = COLORS[COLOR_INDEX]
                COLOR_INDEX += 1
            runtimes = [
                df_id_local[(df_id_local["config::btree_variant"] == btree_variant)][
                    RUNTIME_KEY
                ].values,
                df_id_remote[(df_id_remote["config::btree_variant"] == btree_variant)][
                    RUNTIME_KEY
                ].values,
            ]
            if any(pd.isna(runtime) for runtime in runtimes):
                # was not measured/something went wrong on this machine
                continue

            ax.spines["top"].set_visible(False)
            ax.spines["right"].set_visible(False)
            ax.spines["left"].set_linewidth(1)
            ax.spines["left"].set_edgecolor("gray")
            ax.spines["left"].set_linestyle((0, (12, 12)))
            if unique_id == "AMD1" or (unique_id == "ARM1" and reliability == True):
                ax.spines["left"].set_visible(False)
            ax.tick_params(axis="both", which="both", length=0)

            runtimes = [float(runtime[0]) for runtime in runtimes]
            x += 2
            ax.set_xticks([1, 4.6], ["Loc", "Rem"])
            bars = ax.bar(
                [x_positions[x + i] for i, _ in enumerate(runtimes)],
                runtimes,
                label=[f"{BTREE_VARIANTS_LABEL[btree_variant]}", "_"],
                zorder=3,
                width=0.91,
                hatch=[
                    BTREE_VARIANTS_HATCH[btree_variant],
                    BTREE_VARIANTS_HATCH[btree_variant],
                ],
                edgecolor=VARIANT_COLOR[btree_variant],
                color="#F3F3F3",
            )
            ax.bar(
                [x_positions[x + i] for i, _ in enumerate(runtimes)],
                runtimes,
                label=["_", "_"],
                zorder=2,
                linewidth=HATCH_WIDTH,
                width=0.91,
                edgecolor=VARIANT_COLOR[btree_variant],
                color="#F3F3F3",
            )

            for bar in bars[1::]:
                bar_x = bar.get_x()
                bar_y = bar.get_height()
                width = bar.get_width()
                for bar_i in np.arange(bar_x, bar_x + width, 0.2):  # Adjust spacing
                    ax.plot(
                        [
                            bar_i,
                            bar_i,
                        ],  # Diagonal line start and end
                        [0.0, bar_y],  # Line height
                        linestyle=(0, (1, 1)),  # Dotted line style
                        color=VARIANT_COLOR[btree_variant],
                        linewidth=2,
                        zorder=3,
                    )
            ax.set_xticks([])
            ax.grid(axis="y")

        # Handle grouping of plots into DRAM and HBM / GPU
        if i > 0 and node_groups_filtered[i - 1][1] == group_id:
            ax.sharey(axes[i - 1])
            ax.tick_params(labelleft=False)
        else:
            if i > 0:
                x_plot_offset += GROUPS_SPACING_SEPARATOR
        move_axis_right(ax, x_plot_offset)
    x_plot_offset = 0
    i = -1
    for reliability, (unique_id, group_id) in [
        (rel, node_group_filtered)
        for node_group_filtered in node_groups_filtered
        for rel in [True, False]
    ]:
        if unique_id == "ARM1":
            df_id = df_arm1[
                (df_arm1["id"] == unique_id)
                & (df_arm1["config::reliability"] == reliability)
            ]
        else:
            df_id = df[
                (df["id"] == unique_id) & (df["config::reliability"] == reliability)
            ]
        if len(df_id) == 0:
            continue
        i += 1
        ax = axes[i - number_columns]
        ax.grid(axis="y")

        ax.yaxis.set_major_formatter(FormatStrFormatter("%.1f"))

        df_id_local = df_id[(df_id["config::run_remote_memory"] == False)]
        df_id_remote = df_id[(df_id["config::run_remote_memory"] == True)]
        x = 0
        naive_runtimes = [
            df_id_local[(df_id_local["config::btree_variant"] == "normal")][
                RUNTIME_KEY
            ].values,
            df_id_remote[(df_id_remote["config::btree_variant"] == "normal")][
                RUNTIME_KEY
            ].values,
        ]
        x = -2
        for hash_join_algo in unique_btree_variants:
            if hash_join_algo == "normal":
                continue
            x_positions = [0, 2.3, 1.1, 3.4]
            runtimes = [
                naive_runtimes[0]
                / df_id_local[(df_id_local["config::btree_variant"] == hash_join_algo)][
                    RUNTIME_KEY
                ].values,
                naive_runtimes[1]
                / df_id_remote[
                    (df_id_remote["config::btree_variant"] == hash_join_algo)
                ][RUNTIME_KEY].values,
            ]
            runtimes = [float(runtime[0]) for runtime in runtimes]
            x += 2
            # ax.set_xticks([0.5, 3], ["Loc", "Rem"])
            ax.set_xticks([])
            print(unique_id)
            print(runtimes)

            ax.spines["top"].set_visible(False)
            ax.spines["right"].set_visible(False)
            ax.spines["left"].set_linewidth(1)
            ax.spines["left"].set_edgecolor("gray")
            ax.spines["left"].set_linestyle((0, (12, 12)))
            if unique_id == "AMD1" or (unique_id == "ARM1" and reliability == True):
                ax.spines["left"].set_visible(False)
            ax.tick_params(axis="both", which="both", length=0)
            bars = ax.bar(
                [x_positions[i + x] for i, _ in enumerate(runtimes)],
                runtimes,
                zorder=3,
                width=0.89,
                hatch=[
                    BTREE_VARIANTS_HATCH[hash_join_algo],
                    BTREE_VARIANTS_HATCH[hash_join_algo],
                ],
                edgecolor=VARIANT_COLOR[hash_join_algo],
                color="#F3F3F3",
            )
            ax.bar(
                [x_positions[i + x] for i, _ in enumerate(runtimes)],
                runtimes,
                label=["_", "_"],
                zorder=2,
                linewidth=HATCH_WIDTH,
                width=0.91,
                edgecolor=VARIANT_COLOR[hash_join_algo],
                color="#F3F3F3",
            )
            for bar in bars[1::2]:
                bar_x = bar.get_x()
                bar_y = bar.get_height()
                width = bar.get_width()
                for bar_i in np.arange(bar_x, bar_x + width, 0.17):  # Adjust spacing
                    ax.plot(
                        [
                            bar_i,
                            bar_i,
                        ],  # Diagonal line start and end
                        [0.0, bar_y],  # Line height
                        linestyle=(0, (1, 1)),  # Dotted line style
                        color=VARIANT_COLOR[hash_join_algo],
                        linewidth=2,
                        zorder=3,
                    )
            if unique_id == "ARM1":
                ax.set_title(
                    f"{unique_id}\nWeak" if reliability else f"{unique_id}\nStrong",
                    fontsize=LABEL_FONT_SIZE - 4,
                    y=-0.25,
                )
            else:
                ax.set_title(
                    unique_id + "\n" + NODEID_TO_RELIABILITY[unique_id],
                    fontsize=LABEL_FONT_SIZE - 4,
                    y=-0.25,
                )
            # if unique_id == "ARM1":
            #    ax.set_title(
            #        f"{unique_id}\nWeak" if reliability else f"{unique_id}\nStrong"
            #    )
            # else:
            #    ax.set_title(unique_id + "\n" + NODEID_TO_RELIABILITY[unique_id])

        # Handle grouping of plots into DRAM and HBM / GPU
        if i > 0 and node_groups_filtered[i - 1][1] == group_id:
            ax.sharey(axes[i + number_columns - 1])
            ax.tick_params(labelleft=False)
        else:
            if i > 0:
                draw_line_left_to_plot(
                    fig,
                    ax,
                    x_plot_offset + 0.0167,
                    GROUPS_MEM_TYPE[node_groups_filtered[i - 1][1]],
                    GROUPS_MEM_TYPE[node_groups_filtered[i][1]],
                )
                x_plot_offset += GROUPS_SPACING_SEPARATOR
        move_axis_right(ax, x_plot_offset)
    initial_handles, initial_labels = axes[0].get_legend_handles_labels()

    custom_legend = [
        Patch(facecolor="None", edgecolor="black", label="Local"),
        Patch(facecolor="None", edgecolor="black", label="Remote"),
    ]

    # Combine existing and custom legend entries
    all_handles = initial_handles + custom_legend
    all_labels = initial_labels + [h.get_label() for h in custom_legend]

    # Update the legend
    axes[0].legend(
        handles=all_handles,
        labels=all_labels,
        bbox_to_anchor=(number_columns / 2.0 + 5, 1.27),
        ncol=5,
        fancybox=True,
        fontsize=LABEL_FONT_SIZE - 4,
    )
    legend_x = 0.845
    legend_y = 0.951
    width = 0.038
    height = 0.02
    for line_y in np.arange(legend_y, legend_y + height, 0.008):
        fig.add_artist(
            plt.Line2D(
                [legend_x, legend_x + width],
                [line_y, line_y],
                color="black",
                linewidth=2,
                linestyle=(0, (1, 1)),
            )
        )
    axes[0].set_ylabel("Runtime [s]\nLookup", fontsize=LABEL_FONT_SIZE)
    axes[number_columns].set_ylabel("Speedup over\nNormal", fontsize=LABEL_FONT_SIZE)
    fig.text(
        0.5,
        0.02,
        "Different Systems",
        ha="center",
        va="center",
        fontsize=LABEL_FONT_SIZE,
    )
    plt.savefig(f"{MT_FINAL_FIGURES_DIR}/btree_large_nodes.pdf")
    plt.show()


if __name__ == "__main__":
    plot_btree_benchmark()
