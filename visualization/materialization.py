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


def get_min_runtime(df, mat_variant):
    min_runtime_index = df[(df["variant"] == mat_variant)]["lookup_runtime"].idxmin()
    best_mate_row = df.loc[min_runtime_index]
    best_mate_runtime = best_mate_row["lookup_runtime"]
    return best_mate_runtime


def plot_materialization_benchmark():
    global COLOR_INDEX
    df = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/materialization")

    unique_ids = df["id"].unique()
    unique_variants = df["variant"].unique()

    fig, axes = plt.subplots(
        1,
        len(unique_ids),
        figsize=(20, 8),
        sharex=True,
    )

    if len(unique_ids) == 1:
        axes = [axes]
    # fig.suptitle(f"Local vs Remote Materialization execution (uniform)", fontsize=18)
    x_plot_offset = 0
    node_groups_filtered = [g for g in GROUPS_FLATTENED if g[0] in unique_ids]
    for i, (unique_id, group_id) in enumerate(node_groups_filtered):
        ax = axes[i]
        df_local = df[(df["id"] == unique_id) & (df["run_remote_memory"] == False)]
        df_remote = df[(df["id"] == unique_id) & (df["run_remote_memory"] == True)]
        x_pos = 0
        for bs_variant in unique_variants:
            if bs_variant not in VARIANT_COLOR:
                VARIANT_COLOR[bs_variant] = COLORS[COLOR_INDEX]
                COLOR_INDEX += 1
            runtimes = [
                get_min_runtime(df_local, bs_variant),
                get_min_runtime(df_remote, bs_variant),
            ]
            print(runtimes)
            runtimes = [float(runtime) for runtime in runtimes]
            print(runtimes)
            ax.bar(
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
        ax.set_title(f"{NODEID_TO_ARCH[unique_id]}")
        ax.set_xticks([], [])
        # Handle grouping of plots into DRAM and HBM / GPU
        if i > 0 and node_groups_filtered[i - 1][1] == group_id:
            ax.sharey(axes[i - 1])
            ax.tick_params(labelleft=False)
        else:
            if i > 0:
                draw_line_left_to_plot(
                    fig,
                    ax,
                    x_plot_offset,
                    GROUPS_MEM_TYPE[node_groups_filtered[i - 1][1]],
                    GROUPS_MEM_TYPE[node_groups_filtered[i][1]],
                )
                x_plot_offset += GROUPS_SPACING_SEPARATOR
        move_axis_right(ax, x_plot_offset)
        ax.grid(True)
    add_fancy_descriptors(
        axes[0], "Lookup runtime", "Different Configurations", len(unique_ids)
    )
    plt.show()


def plot_materialization_speedup():
    df = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/materialization")

    unique_ids = df["id"].unique()
    unique_variants = df["variant"].unique()

    fig, axes = plt.subplots(
        1,
        len(unique_ids),
        figsize=(20, 8),
        sharex=True,
    )

    if len(unique_ids) == 1:
        axes = [axes]
    # if :
    #    axes = [[ax] for ax in axes]
    # fig.suptitle(f"Local vs Remote Materialization execution (uniform)", fontsize=18)
    x_plot_offset = 0
    node_groups_filtered = [g for g in GROUPS_FLATTENED if g[0] in unique_ids]
    for i, (unique_id, group_id) in enumerate(node_groups_filtered):
        ax = axes[i]
        df_local = df[(df["id"] == unique_id) & (df["run_remote_memory"] == False)]
        df_remote = df[(df["id"] == unique_id) & (df["run_remote_memory"] == True)]
        naive_runtimes = [
            get_min_runtime(df_local, "baseline_naive"),
            get_min_runtime(df_remote, "baseline_naive"),
        ]
        x_pos = 0
        for bs_variant in unique_variants:
            if bs_variant == "baseline_naive":
                continue
            speedups = [
                naive_runtimes[0] / get_min_runtime(df_local, bs_variant),
                naive_runtimes[1] / get_min_runtime(df_local, bs_variant),
            ]
            print(speedups)
            speedups = [float(speedup) for speedup in speedups]
            print(speedups)
            ax.bar(
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
        ax.set_title(f"{NODEID_TO_ARCH[unique_id]}")
        ax.set_xticks([], [])
        # Handle grouping of plots into DRAM and HBM / GPU
        if i > 0 and node_groups_filtered[i - 1][1] == group_id:
            ax.sharey(axes[i - 1])
            ax.tick_params(labelleft=False)
        else:
            if i > 0:
                draw_line_left_to_plot(
                    fig,
                    ax,
                    x_plot_offset,
                    GROUPS_MEM_TYPE[node_groups_filtered[i - 1][1]],
                    GROUPS_MEM_TYPE[node_groups_filtered[i][1]],
                )
                x_plot_offset += GROUPS_SPACING_SEPARATOR
        move_axis_right(ax, x_plot_offset)
        ax.grid(True)
    add_fancy_descriptors(
        axes[0], "Speedup", "Different Configurations", len(unique_ids)
    )
    plt.show()
    plt.subplots_adjust(hspace=0.3)  # Increase the space between subplots


if __name__ == "__main__":
    plot_materialization_benchmark()
    plot_materialization_speedup()
