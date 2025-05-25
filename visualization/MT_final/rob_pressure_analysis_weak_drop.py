import json
import os
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import seaborn as sns
import numpy as np

from import_helper import *

DATA = "rob_pressure_finaleeee_second_run"

MARKERS = ["*", "|", "o", "x"]
LINESTYLES = ["solid", "solid", "dashed", "dashed"]

START_FROM_INDEX = 1
SLICE = 1
LOCALITIES = ["NTA"]
UNIQUE_IDS = [
    "AMD2",
]

PERF_COUNTERS = [
    "perf::DEMAND_DATA_CACHE_FILLS_FROM_SYSTEM.MEM_IO_LCL",
    "perf::DEMAND_DATA_CACHE_FILLS_FROM_SYSTEM.MEM_IO_RMT",
]
PERF_COUNTERS_LABELS = [
    "Local Prefetched - Demand Load Ratio",
    "Remote Prefetched - Demand Load Ratio",
]

plt.rcParams["xtick.labelsize"] = 22  # X tick label font size
plt.rcParams["ytick.labelsize"] = 22  # Y tick label font size


def get_unique_data(data):
    df = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/{data}")
    unique_ids = [id.split(" - ")[0] for id in UNIQUE_IDS]
    unique_localities = df["config::locality_hint"].unique()
    xs = df["config::num_instructions"].unique()[START_FROM_INDEX::SLICE]
    df_id = df[
        (df["id"] == unique_ids[0])
        & (df["config::locality_hint"] == unique_localities[0])
    ].sort_values(by=["config::num_instructions"])
    return df_id, xs, unique_ids


def add_fancy_labels_and_legend(fig, axes, ylabel):
    fig.text(
        0.5,
        0.01,
        "Number of Added Nops per Resolve",
        ha="center",
        va="bottom",
        fontsize=28,
    )
    plt.tight_layout(rect=[0.015, 0.08, 1, 0.9])
    plt.subplots_adjust(hspace=0.45)  # Increase the space between subplots
    axes[0].legend(
        loc="lower center",
        bbox_to_anchor=(0.45, 1.02),
        ncol=3,
        fancybox=True,
        shadow=False,
        fontsize=27,
    )
    plt.show()


def split_into_local_remote_prefetch_noPrefetch(df_id):
    local_no_prefetch = df_id[
        (df_id["config::prefetch"] == False)
        & (df_id["config::run_remote_memory"] == False)
    ][START_FROM_INDEX::SLICE]
    local_prefetch = df_id[
        (df_id["config::prefetch"] == True)
        & (df_id["config::run_remote_memory"] == False)
    ][START_FROM_INDEX::SLICE]
    remote_no_prefetch = df_id[
        (df_id["config::prefetch"] == False)
        & (df_id["config::run_remote_memory"] == True)
    ][START_FROM_INDEX::SLICE]
    remote_prefetch = df_id[
        (df_id["config::prefetch"] == True)
        & (df_id["config::run_remote_memory"] == True)
    ][START_FROM_INDEX::SLICE]
    return local_no_prefetch, local_prefetch, remote_no_prefetch, remote_prefetch


def plot_rob_pressure_analysis():
    df_id, xs, unique_ids = get_unique_data(DATA)
    (
        perf_df_runtime_no_prefetch,
        perf_df_runtime_prefetch,
        perf_df_runtime_no_prefetch_remote,
        perf_df_runtime_prefetch_remote,
    ) = split_into_local_remote_prefetch_noPrefetch(
        df_id[df_id["config::profile"] == True]
    )
    data_df = pd.DataFrame(
        {
            "x": xs,
            "Local Prefetched": perf_df_runtime_prefetch["access_runtime"].values
            * 1000000000,
            "Remote Prefetched": perf_df_runtime_prefetch_remote[
                "access_runtime"
            ].values
            * 1000000000,
        }
    )
    fig, axes = plt.subplots(3, 1, figsize=(15, 20))
    axes[0].set_xlabel(" ")
    axes[0].set_yscale("log")
    for label, marker, lnstyle in zip(
        ["Local Prefetched", "Remote Prefetched"],
        MARKERS,
        LINESTYLES,
    ):
        axes[0].plot(
            data_df["x"],
            data_df[label],
            label=label,
            marker=marker,
            linestyle=lnstyle,
        )
    for i, (df, perf_counter, label) in enumerate(
        zip(
            [perf_df_runtime_prefetch, perf_df_runtime_prefetch_remote],
            PERF_COUNTERS,
            PERF_COUNTERS_LABELS,
        )
    ):
        axes[1].plot(data_df["x"], df[perf_counter], label=label, marker=MARKERS[i % 2])
    axes[2].plot(
        data_df["x"],
        perf_df_runtime_prefetch["perf::ALLOC_MAB_COUNT"]
        / perf_df_runtime_prefetch["perf::cycles"],
        label=f"Local Prefetched - MLP",
        marker=MARKERS[2],
    )
    axes[2].plot(
        data_df["x"],
        perf_df_runtime_prefetch_remote["perf::ALLOC_MAB_COUNT"]
        / perf_df_runtime_prefetch_remote["perf::cycles"],
        label=f"Remote Prefetched - MLP",
        marker=MARKERS[1],
    )
    axes[2].set_yticks([2, 5, 8])
    axes[2].set_yticklabels(["2", "5", "8"])

    axes[0].set_yscale("log")
    axes[0].legend([], [], frameon=False)
    axes[0].set_xticks(xs)
    axes[0].set_xticklabels(xs)
    axes[0].set_yticks([10, 100])
    axes[0].set_yticklabels(["10", "100"])
    axes[0].set_ylim(3, 150)
    axes[1].set_ylim(0, 1)
    axes[2].set_ylim(0, 9)
    for ax, label in zip(
        axes, ["Runtime per\nAccess [ns]", "Demand Loads\nper Access", "MLP\n"]
    ):
        ax.set_xscale("log")
        ax.xaxis.set_minor_locator(ticker.NullLocator())

        ax.set_xticks(xs)
        ax.set_xticklabels(xs)
        ax.set_ylabel(label, fontsize=28)
        ax.grid(axis="y")
        ax.set_xticks(
            [
                2,
                3,
                5,
                6,
                11,
                14,
                18,
                24,
                31,
                41,
                53,
                69,
                90,
                # 118,
                153,
                # 199,
                259,
                # 337,
                438,
                # 570,
                # 741,
                963,
                # 1252,
                # 1628,
                2115,
            ]
        )
        ymin, ymax = ax.get_ylim()
        ax.vlines(
            90, ymin, ymax, linestyles="dashed", color="green", label="Runtime Drop"
        )
        ax.vlines(438, ymin, ymax, linestyles="dashed", color="green")
        ax.spines["top"].set_linewidth(1)
        ax.spines["top"].set_edgecolor("lightgray")
        ax.spines["top"].set_linestyle((0, (12, 12)))
        ax.spines["right"].set_linewidth(1)
        ax.spines["right"].set_edgecolor("lightgray")
        ax.spines["right"].set_linestyle((0, (12, 12)))

    add_fancy_labels_and_legend(fig, axes, "Runtime per Access [ns]")
    fig.savefig(f"{MT_FINAL_FIGURES_DIR}/rob_pressure_analysis_weak_drop.pdf")


if __name__ == "__main__":
    plot_rob_pressure_analysis()
