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
plt.rcParams["hatch.linewidth"] = 7
HATCH_WIDTH = 5
VARIANT_COLOR = {}
COLOR_INDEX = 0
LABEL_FONT_SIZE = 28
plt.rcParams["hatch.linewidth"] = HATCH_WIDTH

RUNTIME_KEY = "lookup_runtime"
MADVISE_HUGEPAGE = True  # TODO: add madvise false plot
FOLDER = "materialization_finale"
MATERIALIZATION_VARIANTS = [
    "baseline_naive",
    "prefetch_naive",
    "baseline_memcpy",
    "prefetch_memcpy",
]
MATERIALIZATION_VARIANTS_LABEL = {
    "baseline_naive": "Normal",
    "prefetch_naive": "Normal Prefetched",
    "baseline_memcpy": "Memcopy",
    "prefetch_memcpy": "Memcopy Prefetched",
}
MATERIALIZATION_VARIANTS_HATCH = {
    "baseline_naive": "/",
    "prefetch_naive": "\\",
    "baseline_memcpy": "x",
    "prefetch_memcpy": "//",
}

def plot_btree_benchmark():
    global COLOR_INDEX
    df = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/{FOLDER}")

    df = df[
        (df["config::profile"] == False)
        & (df["config::madvise_huge_pages"] == MADVISE_HUGEPAGE)
    ]
    unique_ids = df["id"].unique()
    unique_bsearch_variants = MATERIALIZATION_VARIANTS

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
    fig.subplots_adjust(top=0.85)
    plt.subplots_adjust(wspace=0.02)
    plt.subplots_adjust(hspace=0.1)
    axes = axes.flatten()
    for i, ax in enumerate(axes):
        ax.spines["top"].set_visible(False)
        ax.spines["right"].set_visible(False)
        ax.spines["left"].set_linewidth(1)
        ax.spines["left"].set_edgecolor("gray")
        ax.spines["left"].set_linestyle((0, (12, 12)))
        if i == 0 or i == 5:
            ax.spines["left"].set_visible(False)
        ax.tick_params(axis="both", which="both", length=0)
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
        # ax.yaxis.set_major_formatter(FormatStrFormatter("%.0f"))
        df_id_local = df_id[(df_id["config::run_remote_memory"] == False)]
        df_id_remote = df_id[(df_id["config::run_remote_memory"] == True)]
        x_positions = [0, 5, 1.13, 6.13, 2.26, 7.26, 3.39, 8.39]
        x = -2
        for bsearch_variant in unique_bsearch_variants:
            if bsearch_variant not in VARIANT_COLOR:
                VARIANT_COLOR[bsearch_variant] = COLORS[COLOR_INDEX]
                COLOR_INDEX += 1
            runtimes = [
                df_id_local[(df_id_local["config::variant"] == bsearch_variant)][
                    RUNTIME_KEY
                ].values.min(),
                df_id_remote[(df_id_remote["config::variant"] == bsearch_variant)][
                    RUNTIME_KEY
                ].values.min(),
            ]
            if any(pd.isna(runtime) for runtime in runtimes):
                # was not measured/something went wrong on this machine
                continue
            ax.grid(True)
            ax.grid(axis="x")
            runtimes = [float(runtime) for runtime in runtimes]
            x += 2
            ax.set_xticks([1.65, 6.65], ["Loc", "Rem"])
            bars = ax.bar(
                [x_positions[x + i] for i, _ in enumerate(runtimes)],
                runtimes,
                label=[f"{MATERIALIZATION_VARIANTS_LABEL[bsearch_variant]}", "_"],
                zorder=3,
                hatch=[
                    MATERIALIZATION_VARIANTS_HATCH[bsearch_variant],
                    MATERIALIZATION_VARIANTS_HATCH[bsearch_variant],
                ],
                width=0.89,
                edgecolor=VARIANT_COLOR[bsearch_variant],
                color="#F3F3F3",
            )
            ax.bar(
                [x_positions[x + i] for i, _ in enumerate(runtimes)],
                runtimes,
                label=["_", "_"],
                zorder=2,
                linewidth=HATCH_WIDTH,
                width=0.91,
                edgecolor=VARIANT_COLOR[bsearch_variant],
                color="#F3F3F3",
            )

            for bar in bars[1::]:
                bar_x = bar.get_x()
                bar_y = bar.get_height()
                width = bar.get_width()
                for bar_i in np.arange(bar_x, bar_x + width, 0.3):  # Adjust spacing
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
            ax.set_xticks([])

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
        df_id = df[(df["id"] == unique_id) & (df["config::reliability"] == reliability)]
        if len(df_id) == 0:
            continue
        i += 1
        ax = axes[i - number_columns]
        # ax.yaxis.set_major_formatter(FormatStrFormatter("%.1f"))

        df_id_local = df_id[(df_id["config::run_remote_memory"] == False)]
        df_id_remote = df_id[(df_id["config::run_remote_memory"] == True)]
        x = -2
        naive_runtimes = [
            df_id_local[(df_id_local["config::variant"] == "baseline_naive")][
                RUNTIME_KEY
            ].values.min(),
            df_id_remote[(df_id_remote["config::variant"] == "baseline_naive")][
                RUNTIME_KEY
            ].values.min(),
        ]
        for hash_join_algo in unique_bsearch_variants:
            if hash_join_algo == "baseline_naive":
                continue
            x_positions = [0, 3.5, 1.03, 4.53, 2.06, 5.56]
            runtimes = [
                naive_runtimes[0]
                / df_id_local[(df_id_local["config::variant"] == hash_join_algo)][
                    RUNTIME_KEY
                ].values.min(),
                naive_runtimes[1]
                / df_id_remote[(df_id_remote["config::variant"] == hash_join_algo)][
                    RUNTIME_KEY
                ].values.min(),
            ]
            runtimes = [float(runtime) for runtime in runtimes]
            x += 2
            # ax.set_xticks([1, 4.5], ["Loc", "Rem"])
            ax.set_xticks([])
            ax.grid(axis="y")

            bars = ax.bar(
                [x_positions[i + x] for i, _ in enumerate(runtimes)],
                runtimes,
                zorder=3,
                hatch=[
                    MATERIALIZATION_VARIANTS_HATCH[hash_join_algo],
                    MATERIALIZATION_VARIANTS_HATCH[hash_join_algo],
                ],
                width=0.89,
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
                for bar_i in np.arange(bar_x, bar_x + width, 0.2):  # Adjust spacing
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
            # ax.set_title(f"{unique_id}")

        # Handle grouping of plots into DRAM and HBM / GPU
        if i > 0 and node_groups_filtered[i - 1][1] == group_id:
            ax.sharey(axes[i + number_columns - 1])
            ax.tick_params(labelleft=False)
        else:
            if i > 0:
                draw_line_left_to_plot(
                    fig,
                    ax,
                    x_plot_offset + 0.0165,
                    GROUPS_MEM_TYPE[node_groups_filtered[i - 1][1]],
                    GROUPS_MEM_TYPE[node_groups_filtered[i][1]],
                    height=0.84,
                )
                x_plot_offset += GROUPS_SPACING_SEPARATOR
        move_axis_right(ax, x_plot_offset)
    legend_entries = []
    for variant in MATERIALIZATION_VARIANTS:
        legend_entries.append(
            Patch(
                facecolor=VARIANT_COLOR[variant],
                label=MATERIALIZATION_VARIANTS_LABEL[variant],
            )
        )

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
        ncol=3,
        fancybox=True,
        fontsize=LABEL_FONT_SIZE - 4,
    )
    axes[0].set_ylabel("Time per\nRow [ns]", fontsize=LABEL_FONT_SIZE)
    axes[number_columns].set_ylabel("Speedup over\nNormal", fontsize=LABEL_FONT_SIZE)
    fig.text(
        0.5,
        0.02,
        "Different Systems",
        ha="center",
        va="center",
        fontsize=LABEL_FONT_SIZE,
    )

    legend_x = 0.748
    legend_y = 0.9
    width = 0.037
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

    plt.savefig(f"{MT_FINAL_FIGURES_DIR}/materialization.pdf")
    plt.show()


if __name__ == "__main__":
    plot_btree_benchmark()
