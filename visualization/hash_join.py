import json
import os
import matplotlib.pyplot as plt
import seaborn as sns


from helper import *
from config import *


VARIANT_COLOR = {}
COLOR_INDEX = 0

RUNTIME_KEY = "probe_runtime"
MADVISE_HUGEPAGE = True
FOLDER = "hash_join"  # hash_join_64_prefetch_distance


def plot_hash_join_benchmark():
    global COLOR_INDEX
    df = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/{FOLDER}")
    print(df.columns)

    df = df[
        (df["config.madvise_huge_pages"] == MADVISE_HUGEPAGE)
        & (df["config.profile"] == False)
    ]
    unique_ids = df["id"].unique()
    unique_hash_join_variants = df["config.algo"].unique()

    fig, axes = plt.subplots(
        1,
        len(unique_ids),
        figsize=(20, 8 * len(unique_ids)),
        sharex=True,
    )

    x_plot_offset = 0
    # fig.suptitle(f"{dis} Distribution", fontsize=18)
    if len(unique_ids) == 1:
        axes = [axes]
    node_groups_filtered = [g for g in GROUPS_FLATTENED if g[0] in unique_ids]
    for i, (unique_id, group_id) in enumerate(node_groups_filtered):
        ax = axes[i]
        df_id = df[(df["id"] == unique_id)]
        df_id_local = df_id[(df_id["config.run_remote_memory"] == False)]
        df_id_remote = df_id[(df_id["config.run_remote_memory"] == True)]
        x = 0
        for hash_join_variant in unique_hash_join_variants:
            if hash_join_variant not in VARIANT_COLOR:
                VARIANT_COLOR[hash_join_variant] = COLORS[COLOR_INDEX]
                COLOR_INDEX += 1
            runtimes = [
                df_id_local[(df_id_local["config.algo"] == hash_join_variant)][
                    RUNTIME_KEY
                ].values,
                df_id_remote[(df_id_remote["config.algo"] == hash_join_variant)][
                    RUNTIME_KEY
                ].values,
            ]
            if any(pd.isna(runtime) for runtime in runtimes):
                # was not measured/something went wrong on this machine
                continue
            runtimes = [float(runtime[0]) for runtime in runtimes]
            x += 2
            ax.bar(
                [i + x for i, _ in enumerate(runtimes)],
                runtimes,
                label=[f"{hash_join_variant} Local", f"{hash_join_variant} Remote"],
                zorder=2,
                linewidth=1.0,
                hatch=["/", ""],
                edgecolor="black",
                color=VARIANT_COLOR[hash_join_variant],
            )
            ax.set_title(f"{NODEID_TO_ARCH[unique_id]}")
            ax.set_xticks([], [])
            ax.grid(True)

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
    add_fancy_descriptors(
        axes[0], RUNTIME_KEY, "Different Configurations", len(unique_ids)
    )
    plt.show()


def plot_hash_join_speedup():
    global COLOR_INDEX
    df = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/{FOLDER}")
    print(df.columns)

    df = df[
        (df["config.madvise_huge_pages"] == MADVISE_HUGEPAGE)
        & (df["config.profile"] == False)
    ]
    unique_ids = df["id"].unique()
    unique_hash_join_variants = df["config.algo"].unique()

    fig, axes = plt.subplots(
        1,
        len(unique_ids),
        figsize=(20, 8 * len(unique_ids)),
        sharex=True,
    )
    # fig.suptitle(f"{dis} Distribution", fontsize=18)
    if len(unique_ids) == 1:
        axes = [axes]
    x_plot_offset = 0
    node_groups_filtered = [g for g in GROUPS_FLATTENED if g[0] in unique_ids]
    for i, (unique_id, group_id) in enumerate(node_groups_filtered):
        ax = axes[i]
        df_id = df[(df["id"] == unique_id)]
        df_id_local = df_id[(df_id["config.run_remote_memory"] == False)]
        df_id_remote = df_id[(df_id["config.run_remote_memory"] == True)]
        x = 0
        naive_runtimes = [
            df_id_local[(df_id_local["config.algo"] == "NPO")][RUNTIME_KEY].values,
            df_id_remote[(df_id_remote["config.algo"] == "NPO")][RUNTIME_KEY].values,
        ]
        for hash_join_algo in unique_hash_join_variants:
            if hash_join_algo == "NPO":
                continue
            x_pos = []
            runtimes = [
                naive_runtimes[0]
                / df_id_local[(df_id_local["config.algo"] == hash_join_algo)][
                    RUNTIME_KEY
                ].values,
                naive_runtimes[1]
                / df_id_remote[(df_id_remote["config.algo"] == hash_join_algo)][
                    RUNTIME_KEY
                ].values,
            ]
            runtimes = [float(runtime[0]) for runtime in runtimes]
            x += 2
            ax.bar(
                [i + x for i, _ in enumerate(runtimes)],
                runtimes,
                label=[f"{hash_join_algo} Local", f"{hash_join_algo} Remote"],
                zorder=2,
                linewidth=1.0,
                hatch=["/", ""],
                edgecolor="black",
                color=VARIANT_COLOR[hash_join_algo],
            )
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
    add_fancy_descriptors(
        axes[0], "Speedup", "Different Configurations", len(unique_ids)
    )
    plt.show()


if __name__ == "__main__":
    plot_hash_join_benchmark()
    plot_hash_join_speedup()
