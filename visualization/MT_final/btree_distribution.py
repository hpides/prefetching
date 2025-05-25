import json
import os
import matplotlib.pyplot as plt
from matplotlib.ticker import FormatStrFormatter
from matplotlib.patches import Patch
from matplotlib.lines import Line2D

import seaborn as sns


from import_helper import *


plt.rcParams["xtick.labelsize"] = 23  # X tick label font size
plt.rcParams["ytick.labelsize"] = 24  # Y tick label font size
plt.rcParams["axes.labelsize"] = 18  # X and Y label font size
HATCH_WIDTH = 2
VARIANT_COLOR = {}
COLOR_INDEX = 0
LABEL_FONT_SIZE = 28
MARKERSIZE = 7
plt.rcParams["hatch.linewidth"] = HATCH_WIDTH

REMOTE_LINE_STYLE = "dotted"
RUNTIME_KEY = "lookup_runtime"
# MADVISE_HUGEPAGE = True (always done)
FOLDER = "btree_benchmark_vary_node_sizes"  # "binary_search_zipf_mix"
ARM_EXTENDED_FOLDER = "btree_benchmark_vary_node_sizes_larger_nodes"
PARALLEL_STREAMS = 20  # 10, 20, 30
BSEARCH_VARIANTS = ["normal", "coro_half_node", "coro_full_node"]
BSEARCH_VARIANTS_LABEL = {
    "normal": "Normal",
    "coro_half_node": "Coro Half Node",
    "coro_full_node": "Coro Full Node",
}
BSEARCH_VARIANTS_MARKER = {
    "normal": "*",
    "coro_half_node": "|",
    "coro_full_node": "x",
}
#    "T2": "|",


def increase_width(axis):
    pos = axis.get_position()
    new_width = pos.width * 1.14
    delta = new_width - pos.width
    new_pos = [pos.x0, pos.y0, new_width, pos.height]
    axis.set_position(new_pos)
    return delta


def plot_btree_benchmark(distribution):
    global COLOR_INDEX
    df_noarm = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/{FOLDER}")
    df_arm = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/{ARM_EXTENDED_FOLDER}"
    )
    print(df_arm)
    print(df_noarm)
    df = pd.concat([df_noarm, df_arm])
    df = df[
        (df["config::profile"] == False)
        & (df["config::coroutines"] == PARALLEL_STREAMS)
        & (df["config::key_distribution"] == distribution)
        & (df["config::num_elements"] == 50000000)
    ].sort_values(by="config::tree_node_size")
    unique_ids = df["id"].unique()
    unique_tree_node_sizes = df["config::tree_node_size"].unique()
    unique_bsearch_variants = BSEARCH_VARIANTS
    print(unique_tree_node_sizes)
    number_columns = len(unique_ids) + 1
    fig, axes = plt.subplots(
        1,
        number_columns,
        figsize=(19, 4),
    )
    axes = axes.flatten()
    x_plot_offset = 0
    # fig.suptitle(f"{dis} Distribution", fontsize=18)
    if len(unique_ids) == 1:
        axes = [axes]
    fig.subplots_adjust(left=0.045)
    fig.subplots_adjust(bottom=0.22)
    fig.subplots_adjust(top=0.80)
    plt.subplots_adjust(wspace=0.1)
    plt.subplots_adjust(hspace=0.1)
    # axes = axes.flatten()
    node_groups_filtered = [g for g in GROUPS_FLATTENED if g[0] in unique_ids]
    x_plot_offset = 0
    i = -1
    for reliability, (unique_id, group_id) in [
        (rel, node_group_filtered)
        for node_group_filtered in node_groups_filtered
        for rel in [True, False]
    ]:
        df_id = df[(df["id"] == unique_id) & (df["config::reliability"] == reliability)]
        if len(df_id) == 0:
            continue
        print(unique_id)
        i += 1
        ax = axes[i]
        ax.set_xscale("log")
        ax.grid(axis="y")
        if distribution == "uniform":
            ax.yaxis.set_major_formatter(FormatStrFormatter("%.0f"))
        else:
            ax.yaxis.set_major_formatter(FormatStrFormatter("%.1f"))

        df_id_local = df_id[(df_id["config::run_remote_memory"] == False)]
        df_id_remote = df_id[(df_id["config::run_remote_memory"] == True)]
        x = 0
        min_runtimes = {}
        for bsearch_variant in unique_bsearch_variants:
            if bsearch_variant not in VARIANT_COLOR:
                VARIANT_COLOR[bsearch_variant] = COLORS[COLOR_INDEX]
                COLOR_INDEX += 1
            runtimes = [
                df_id_local[(df_id_local["config::btree_variant"] == bsearch_variant)][
                    RUNTIME_KEY
                ].values,
                df_id_remote[
                    (df_id_remote["config::btree_variant"] == bsearch_variant)
                ][RUNTIME_KEY].values,
            ]
            print(
                df_id_local[(df_id_local["config::btree_variant"] == bsearch_variant)][
                    RUNTIME_KEY
                ]
            )
            print(runtimes)
            min_runtimes[bsearch_variant] = min(runtimes[1])
            x += 2.2
            ax.spines["top"].set_linewidth(1)
            ax.spines["top"].set_edgecolor("lightgray")
            ax.spines["top"].set_linestyle((0, (12, 12)))
            ax.spines["right"].set_linewidth(1)
            ax.spines["right"].set_edgecolor("lightgray")
            ax.spines["right"].set_linestyle((0, (12, 12)))

            ax.tick_params(axis="y", which="both", length=0)
            ax.tick_params(axis="x", which="minor", length=0)
            ax.plot(
                unique_tree_node_sizes[: len(runtimes[1])],
                runtimes[1],
                label=BSEARCH_VARIANTS_LABEL[bsearch_variant],
                zorder=2,
                linewidth=HATCH_WIDTH,
                color=VARIANT_COLOR[bsearch_variant],
            )
            ax.plot(
                unique_tree_node_sizes[: len(runtimes[1])],
                runtimes[1][::],
                BSEARCH_VARIANTS_MARKER[bsearch_variant],
                label="_",
                zorder=2,
                color=VARIANT_COLOR[bsearch_variant],
                markersize=MARKERSIZE,
            )

            # ax.plot(
            #    unique_zipf_parameters,
            #    runtimes[1],
            #    label=f"_",
            #    linestyle=REMOTE_LINE_STYLE,
            #    zorder=2,
            #    linewidth=HATCH_WIDTH,
            #    color=VARIANT_COLOR[bsearch_variant],
            #    markersize=MARKERSIZE,
            # )
            # ax.plot(
            #    unique_zipf_parameters[::],
            #    runtimes[1][::],
            #    BSEARCH_VARIANTS_MARKER[bsearch_variant],
            #    label=f"_",
            #    linestyle=REMOTE_LINE_STYLE,
            #    zorder=2,
            #    linewidth=HATCH_WIDTH,
            #    color=VARIANT_COLOR[bsearch_variant],
            #    markersize=MARKERSIZE,
            # )
            title = NODEID_TO_LABEL[unique_id] + "\n" + NODEID_TO_RELIABILITY[unique_id]
            if unique_id == "ARM1":
                title = (
                    f"{NODEID_TO_LABEL[unique_id]}\nWeak"
                    if reliability
                    else f"{NODEID_TO_LABEL[unique_id]}\nStrong"
                )

            # ax.set_title(
            #    unique_id + "\n" + NODEID_TO_RELIABILITY[unique_id],
            #    fontsize=LABEL_FONT_SIZE - 4,
            # )
            ax.text(
                4200 if unique_id == "ARM1" else 2100,
                45 if i > 4 else 27,
                title,
                ha="center",
                va="top",
                fontsize=21,
                bbox=dict(facecolor="white", edgecolor="none", boxstyle="round,pad=0"),
            )
        print(
            max(
                [
                    min_runtimes["normal"] / min_runtimes["coro_half_node"],
                    min_runtimes["normal"] / min_runtimes["coro_full_node"],
                ]
            )
        )
        # Handle grouping of plots into DRAM and HBM / GPU
        if i > 0 and node_groups_filtered[i - 1][1] == group_id:
            ax.sharey(axes[i - 1])
            ax.tick_params(labelleft=False)
        else:
            if i > 0:
                draw_line_left_to_plot(
                    fig,
                    ax,
                    x_plot_offset + 0.015,
                    GROUPS_MEM_TYPE[node_groups_filtered[i - 1][1]],
                    GROUPS_MEM_TYPE[node_groups_filtered[i][1]],
                    height=0.8,
                    lower=0.15,
                )
                x_plot_offset += GROUPS_SPACING_SEPARATOR
        move_axis_right(ax, x_plot_offset)
        if i == 5 or i == 6:
            x_plot_offset += increase_width(ax)
    initial_handles, initial_labels = axes[0].get_legend_handles_labels()

    custom_legend = [
        Line2D(
            [0],
            [0],
            color=VARIANT_COLOR[bsearch_variant],
            marker=BSEARCH_VARIANTS_MARKER[bsearch_variant],
            label=BSEARCH_VARIANTS_LABEL[bsearch_variant],
            markersize=10,
        )
        for bsearch_variant in BSEARCH_VARIANTS
        #    Line2D([0], [0], color="black", label="Local"),
        #    Line2D([0], [0], color="black", linestyle="dotted", label="Remote"),
    ]

    # Combine existing and custom legend entries
    all_handles = custom_legend
    all_labels = initial_labels + [h.get_label() for h in custom_legend]
    for ax, long in zip(axes, [False, False, False, False, False, True, True, False]):
        label = [".25", "1", "4", "16", "64"] if long else [".25", "1", "4", "16"]

        ylim = ax.get_ylim()
        ax.set_xticks(
            unique_tree_node_sizes[1::2] if long else unique_tree_node_sizes[1:-2:2],
            label,
        )
        # for tick, lbl in zip(ax.xaxis.get_ticklabels(), label):
        #    if "/" in lbl:  # Check for LaTeX fractions
        #        tick.set_fontsize(23)  # Smaller size for fractions
        ax.set_ylim(ylim)
    # Update the legend
    axes[0].legend(
        handles=all_handles,
        labels=all_labels,
        loc="lower center",
        bbox_to_anchor=(number_columns / 2.0 + 0.9, 0.98),
        ncol=5,
        fancybox=True,
        fontsize=LABEL_FONT_SIZE - 4,
    )
    axes[0].set_ylabel("Runtime [s]", fontsize=LABEL_FONT_SIZE)
    fig.text(
        0.5,
        0.05,
        "B-Tree Node Size [KiB]",
        ha="center",
        va="center",
        fontsize=LABEL_FONT_SIZE,
    )
    axes[0].set_yticks([5, 10, 15, 20, 25], [5, 10, 15, 20, 25])
    axes[6].set_yticks([10, 20, 30, 40], [10, 20, 30, 40])
    plt.savefig(f"{MT_FINAL_FIGURES_DIR}/btree_diff_node_sizes.pdf")
    plt.show()


if __name__ == "__main__":
    plot_btree_benchmark("uniform")
