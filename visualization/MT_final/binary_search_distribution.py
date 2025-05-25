import json
import os
import matplotlib.pyplot as plt
from matplotlib.ticker import FormatStrFormatter
from matplotlib.patches import Patch
from matplotlib.lines import Line2D

import seaborn as sns


from import_helper import *


plt.rcParams["xtick.labelsize"] = 24  # X tick label font size
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
FOLDER = "binary_search_zipf_finaleeeeee"  # "binary_search_zipf_mix"
PARALLEL_STREAMS = 30  # 10, 20, 30
BSEARCH_VARIANTS = ["naive", "coro", "state"]
BSEARCH_VARIANTS_LABEL = {
    "naive": "Normal",
    "coro": "Coroutine",
    "state": "State Machine",
}
BSEARCH_VARIANTS_MARKER = {
    "naive": "*",
    "coro": "|",
    "state": "x",
}
#    "T2": "|",


def plot_btree_benchmark(distribution):
    global COLOR_INDEX
    df = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/{FOLDER}")

    df = df[
        (df["config::profile"] == False)
        & (df["config::parallel_streams"] == PARALLEL_STREAMS)
        & (df["config::key_distribution"] == distribution)
        & (df["config::num_elements"] == 100000000)
    ].sort_values(by="config::zipf_theta")
    unique_ids = df["id"].unique()
    unique_zipf_parameters = df["config::zipf_theta"].unique()
    unique_bsearch_variants = BSEARCH_VARIANTS
    print(unique_zipf_parameters)
    number_columns = len(unique_ids) + 1
    fig, axes = plt.subplots(
        1,
        number_columns,
        figsize=(19, 4.2),
    )
    axes = axes.flatten()
    x_plot_offset = 0
    # fig.suptitle(f"{dis} Distribution", fontsize=18)
    if len(unique_ids) == 1:
        axes = [axes]
    fig.subplots_adjust(left=0.04)
    fig.subplots_adjust(bottom=0.17)
    fig.subplots_adjust(top=0.68)
    plt.subplots_adjust(wspace=0.1)
    plt.subplots_adjust(hspace=0.25)
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
                df_id_local[
                    (df_id_local["config::binary_search_variant"] == bsearch_variant)
                ][RUNTIME_KEY].values,
                df_id_remote[
                    (df_id_remote["config::binary_search_variant"] == bsearch_variant)
                ][RUNTIME_KEY].values,
            ]
            min_runtimes[bsearch_variant] = (runtimes[0][0], runtimes[0][1])
            # speedups = [
            #    naive_runtimes[0]  # local
            #    / df_id_local[
            #        (df_id_local["config::binary_search_variant"] == bsearch_variant)
            #    ][RUNTIME_KEY].values,
            #    naive_runtimes[1]  # remote
            #    / df_id_remote[
            #        (df_id_remote["config::binary_search_variant"] == bsearch_variant)
            #    ][RUNTIME_KEY].values,
            # ]
            x += 2.2
            ax.spines["top"].set_linewidth(1)
            ax.spines["top"].set_edgecolor("lightgray")
            ax.spines["top"].set_linestyle((0, (12, 12)))
            ax.spines["right"].set_linewidth(1)
            ax.spines["right"].set_edgecolor("lightgray")
            ax.spines["right"].set_linestyle((0, (12, 12)))

            ax.tick_params(axis="both", which="both", length=0)
            ax.plot(
                unique_zipf_parameters,
                runtimes[0],
                label=BSEARCH_VARIANTS_LABEL[bsearch_variant],
                zorder=2,
                linewidth=HATCH_WIDTH,
                color=VARIANT_COLOR[bsearch_variant],
            )
            ax.plot(
                unique_zipf_parameters[::3],
                runtimes[0][::3],
                BSEARCH_VARIANTS_MARKER[bsearch_variant],
                label="_",
                zorder=2,
                color=VARIANT_COLOR[bsearch_variant],
                markersize=MARKERSIZE,
            )
            ax.plot(
                unique_zipf_parameters,
                runtimes[1],
                label=f"_",
                linestyle=REMOTE_LINE_STYLE,
                zorder=2,
                linewidth=HATCH_WIDTH,
                color=VARIANT_COLOR[bsearch_variant],
                markersize=MARKERSIZE,
            )
            ax.plot(
                unique_zipf_parameters[::3],
                runtimes[1][::3],
                BSEARCH_VARIANTS_MARKER[bsearch_variant],
                label=f"_",
                linestyle=REMOTE_LINE_STYLE,
                zorder=2,
                linewidth=HATCH_WIDTH,
                color=VARIANT_COLOR[bsearch_variant],
                markersize=MARKERSIZE,
            )
            if unique_id == "ARM1":
                ax.set_title(
                    (
                        f"{NODEID_TO_LABEL[unique_id]}\nWeak"
                        if reliability
                        else f"{NODEID_TO_LABEL[unique_id]}\nStrong"
                    ),
                    fontsize=LABEL_FONT_SIZE - 4,
                )
            else:
                ax.set_title(
                    NODEID_TO_LABEL[unique_id]
                    + "\n"
                    + NODEID_TO_RELIABILITY[unique_id],
                    fontsize=LABEL_FONT_SIZE - 4,
                )
        print(
            max(
                [
                    min_runtimes["naive"][0] / min_runtimes["coro"][0],
                    min_runtimes["naive"][0] / min_runtimes["coro"][0],
                ]
            )
        )
        print(
            "remote: "
            + str(
                max(
                    [
                        min_runtimes["naive"][1] / min_runtimes["state"][1],
                        min_runtimes["naive"][1] / min_runtimes["state"][1],
                    ]
                )
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
                )
                x_plot_offset += GROUPS_SPACING_SEPARATOR
        move_axis_right(ax, x_plot_offset)
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
    ] + [
        Line2D([0], [0], color="black", label="Local"),
        Line2D([0], [0], color="black", linestyle="dotted", label="Remote"),
    ]

    # Combine existing and custom legend entries
    all_handles = custom_legend
    all_labels = initial_labels + ["Local", "Remote"]

    # Update the legend
    axes[0].legend(
        handles=all_handles,
        labels=all_labels,
        loc="lower center",
        bbox_to_anchor=(number_columns / 2.0 + 0.9, 1.27),
        ncol=5,
        fancybox=True,
        fontsize=LABEL_FONT_SIZE - 4,
    )
    axes[0].set_ylabel("Runtime [s]", fontsize=LABEL_FONT_SIZE)
    fig.text(
        0.5,
        0.04,
        "Zipf Parameter $\Theta$",
        ha="center",
        va="center",
        fontsize=LABEL_FONT_SIZE,
    )
    axes[0].set_yticks([1, 2, 3, 4], ["1", "2", "3", "4"])
    plt.savefig(f"{MT_FINAL_FIGURES_DIR}/binary_search_zipf_distribution.pdf")
    plt.show()


if __name__ == "__main__":
    plot_btree_benchmark("zip")
