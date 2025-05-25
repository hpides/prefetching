import json
import os
import matplotlib.pyplot as plt
from matplotlib.ticker import FormatStrFormatter
from matplotlib.patches import Patch
from matplotlib.patches import Rectangle
import numpy as np
import seaborn as sns


from import_helper import *


plt.rcParams["xtick.labelsize"] = 24  # X tick label font size
plt.rcParams["ytick.labelsize"] = 24  # Y tick label font size
plt.rcParams["axes.labelsize"] = 18  # X and Y label font size
plt.rcParams["hatch.linewidth"] = 10
HATCH_WIDTH = 8
VARIANT_COLOR = {}
COLOR_INDEX = 0
LABEL_FONT_SIZE = 28
plt.rcParams["hatch.linewidth"] = HATCH_WIDTH

RUNTIME_KEY = "lookup_runtime"
# MADVISE_HUGEPAGE = True (always done)
FOLDER = "binary_search_original_callbacks"
PARALLEL_STREAMS = 30  # 10, 20, 30
BSEARCH_VARIANTS = ["naive", "coro", "state"]
BSEARCH_VARIANTS_LABEL = {
    "naive": "Normal",
    "coro": "Coroutine",
    "state": "State Machine",
}
BSEARCH_VARIANTS_HATCH = {
    "naive": "/",
    "coro": "\\",
    "state": "x",
}


def plot_btree_benchmark(distribution):
    global COLOR_INDEX
    df = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/{FOLDER}")

    df = df[
        (df["config::profile"] == False)
        & (df["config::parallel_streams"] == PARALLEL_STREAMS)
        & (df["config::key_distribution"] == distribution)
        & (df["config::num_elements"] == 100000000)
    ]
    unique_ids = df["id"].unique()
    unique_bsearch_variants = BSEARCH_VARIANTS

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
    fig.subplots_adjust(bottom=0.18)
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

        df_id = df[(df["id"] == unique_id) & (df["config::reliability"] == reliability)]
        if len(df_id) == 0:
            continue
        i += 1
        ax = axes[i]
        # if distribution == "uniform":
        #    ax.yaxis.set_major_formatter(FormatStrFormatter("%.0f"))
        # else:
        ax.yaxis.set_major_formatter(FormatStrFormatter("%.1f"))

        df_id_local = df_id[(df_id["config::run_remote_memory"] == False)]
        df_id_remote = df_id[(df_id["config::run_remote_memory"] == True)]
        x = 0
        for bsearch_variant in unique_bsearch_variants:
            if bsearch_variant not in VARIANT_COLOR:
                VARIANT_COLOR[bsearch_variant] = COLORS[COLOR_INDEX]
                COLOR_INDEX += 1
            runtimes = [
                df_id_local[
                    (df_id_local["config::binary_search_variant"] == bsearch_variant)
                ][RUNTIME_KEY].values,
                df_id_remote[
                    (df_id_remote["config::binary_search_variant"] == bsearch_variant)
                ][RUNTIME_KEY].values,
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
            x += 2.4
            bars = ax.bar(
                [i + x for i, _ in enumerate(runtimes)],
                runtimes,
                label=[f"{BSEARCH_VARIANTS_LABEL[bsearch_variant]}", "_"],
                zorder=3,
                width=0.91,
                hatch=[
                    BSEARCH_VARIANTS_HATCH[bsearch_variant],
                    BSEARCH_VARIANTS_HATCH[bsearch_variant],
                ],
                edgecolor=VARIANT_COLOR[bsearch_variant],
                color="#F3F3F3",
            )
            ax.bar(
                [i + x for i, _ in enumerate(runtimes)],
                runtimes,
                label=["_", "_"],
                zorder=2,
                linewidth=HATCH_WIDTH,
                width=0.91,
                edgecolor=VARIANT_COLOR[bsearch_variant],
                color="#F3F3F3",
            )

            for bar in bars[1::2]:
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
                        color=VARIANT_COLOR[bsearch_variant],
                        linewidth=2,
                        zorder=3,
                    )

            # ax.set_xticks(
            #    [x + (y / 10.0) for y in range(24, 84, 24) for x in [0, 1]],
            #    ["Loc", "Rem", "Loc", "Rem", "Loc", "Rem"],
            # )

            ax.set_xticks([])
            ax.tick_params(axis="x", rotation=90)
            ax.grid(axis="y")

        # Handle grouping of plots into DRAM and HBM / GPU
        if unique_id == "ARM1" and distribution == "uniform":
            ax.set_ylim([0, 9])
        if unique_id == "AMD1" and distribution == "zip":
            ax.set_ylim([0, 1.48])
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
        df_id = df[(df["id"] == unique_id) & (df["config::reliability"] == reliability)]
        if len(df_id) == 0:
            continue
        i += 1
        ax = axes[i - number_columns]
        ax.grid(axis="y")
        # if distribution == "uniform":
        #    ax.yaxis.set_major_formatter(FormatStrFormatter("%.0f"))
        # else:
        ax.yaxis.set_major_formatter(FormatStrFormatter("%.1f"))

        df_id_local = df_id[(df_id["config::run_remote_memory"] == False)]
        df_id_remote = df_id[(df_id["config::run_remote_memory"] == True)]
        x = 0
        naive_runtimes = [
            df_id_local[(df_id_local["config::binary_search_variant"] == "naive")][
                RUNTIME_KEY
            ].values,
            df_id_remote[(df_id_remote["config::binary_search_variant"] == "naive")][
                RUNTIME_KEY
            ].values,
        ]
        for bsearch_variant in unique_bsearch_variants:
            if bsearch_variant == "naive":
                continue
            x_pos = []
            runtimes = [
                naive_runtimes[0]
                / df_id_local[
                    (df_id_local["config::binary_search_variant"] == bsearch_variant)
                ][RUNTIME_KEY].values,
                naive_runtimes[1]
                / df_id_remote[
                    (df_id_remote["config::binary_search_variant"] == bsearch_variant)
                ][RUNTIME_KEY].values,
            ]
            runtimes = [float(runtime[0]) for runtime in runtimes]
            x += 2.2
            # ax.set_xticks([0.5, 3], ["Loc", "Rem"])
            ax.spines["top"].set_visible(False)
            ax.spines["right"].set_visible(False)
            ax.spines["left"].set_linewidth(1)
            ax.spines["left"].set_edgecolor("gray")
            ax.spines["left"].set_linestyle((0, (12, 12)))
            if unique_id == "AMD1" or (unique_id == "ARM1" and reliability == True):
                ax.spines["left"].set_visible(False)

            ax.tick_params(axis="both", which="both", length=0)
            bars = ax.bar(
                [i + x for i, _ in enumerate(runtimes)],
                runtimes,
                label=["_", "_"],
                zorder=3,
                width=0.89,
                hatch=[
                    BSEARCH_VARIANTS_HATCH[bsearch_variant],
                    BSEARCH_VARIANTS_HATCH[bsearch_variant],
                ],
                edgecolor=VARIANT_COLOR[bsearch_variant],
                color="#F3F3F3",
            )
            ax.bar(
                [i + x for i, _ in enumerate(runtimes)],
                runtimes,
                label=["_", "_"],
                zorder=2,
                linewidth=HATCH_WIDTH,
                width=0.91,
                edgecolor=VARIANT_COLOR[bsearch_variant],
                color="#F3F3F3",
            )

            for bar in bars[1::2]:
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
                        color=VARIANT_COLOR[bsearch_variant],
                        linewidth=2,
                        zorder=3,
                    )
            if unique_id == "ARM1":
                ax.set_title(
                    (
                        f"{NODEID_TO_LABEL[unique_id]}\nWeak"
                        if reliability
                        else f"{NODEID_TO_LABEL[unique_id]}\nStrong"
                    ),
                    fontsize=LABEL_FONT_SIZE - 4,
                    y=-0.25,
                )
            else:
                ax.set_title(
                    NODEID_TO_LABEL[unique_id]
                    + "\n"
                    + NODEID_TO_RELIABILITY[unique_id],
                    fontsize=LABEL_FONT_SIZE - 4,
                    y=-0.25,
                )
            # ax.set_title(f"{unique_id}")
            ax.set_xticks([])
            # ax.tick_params(axis="x", rotation=90)

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
        if unique_id == "ARM1":
            if distribution == "uniform":
                ax.set_ylim([0, 3.5])
            else:
                ax.set_ylim([0, 2.2])

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
        loc="lower center",
        bbox_to_anchor=(number_columns / 2.0 + 0.5, 1),
        ncol=5,
        fancybox=True,
        fontsize=LABEL_FONT_SIZE - 4,
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
    # coordinates on the top left of legend:
    legend_x = 0.829
    legend_y = 0.958
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

    plt.savefig(
        f"{MT_FINAL_FIGURES_DIR}/binary_search_{distribution}_old_callbacks.pdf"
    )
    plt.show()


if __name__ == "__main__":
    plot_btree_benchmark("uniform")
    plot_btree_benchmark("zip")
