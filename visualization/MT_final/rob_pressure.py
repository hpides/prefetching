import json
import os
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import seaborn as sns
import numpy as np

from import_helper import *

plt.rcParams["xtick.labelsize"] = 21  # X tick label font size
plt.rcParams["ytick.labelsize"] = 21  # Y tick label font size

DATA = (
    "rob_pressure_finaleeee"  # "rob_pressure_nops_no_jemalloc" ,"rob_pressure_ca_alloc"
)
# rob_pressure_camel_finaleeee


MARKERS = ["*", "|", "o", "x"]
LINESTYLES = ["solid", "solid", "dashed", "dashed"]

START_FROM_INDEX = 1
SLICE = 2
LOCALITIES = ["NTA"]
UNIQUE_IDS = [
    "AMD1",
    "INTEL1",
    "AMD2",
    "INTEL2",
    "ARM2",
    "INTEL3",
    "ARM1 - Weak",
    "ARM1 - Strong",
]

def get_unique_datas(data):
    df = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/{data}")

    unique_ids = [id.split(" - ")[0] for id in UNIQUE_IDS]
    labels = UNIQUE_IDS
    reliabilities = [
        True if id.split(" - ")[-1] == "Weak" else False for id in UNIQUE_IDS
    ]
    unique_localities = df["config::locality_hint"].unique()
    xs = df["config::num_instructions"].unique()[START_FROM_INDEX::SLICE]
    print(df)
    print(unique_localities)
    for unique_locality in LOCALITIES:
        fig, axes = plt.subplots(
            int(np.ceil(len(unique_ids) / 2)),
            2,
            figsize=(15, 10),
        )

        axes = axes.ravel()

        for i, (ax, unique_id, reliability, label) in enumerate(
            zip(axes, unique_ids, reliabilities, labels)
        ):
            ax.spines["top"].set_linewidth(1)
            ax.spines["top"].set_edgecolor("lightgray")
            ax.spines["top"].set_linestyle((0, (12, 12)))
            ax.spines["right"].set_linewidth(1)
            ax.spines["right"].set_edgecolor("lightgray")
            ax.spines["right"].set_linestyle((0, (12, 12)))
            df_id = df[
                (df["id"] == unique_id)
                & (df["config::locality_hint"] == unique_locality)
                & (df["config::profile"] == False)
                & (df["config::reliability"] == reliability)
            ].sort_values(by=["config::num_instructions"])
            if len(df_id) == 0:
                yield axes, i, ax, df_id, unique_id, fig, [], label
            else:
                yield axes, i, ax, df_id, unique_id, fig, xs, label


def add_fancy_labels_and_legend(fig, axes, ylabel):
    fig.text(
        0.5,
        0.005,
        "Number of Added NOP per Resolve",
        ha="center",
        va="bottom",
        fontsize=30,
    )
    fig.text(
        0.005,
        0.5,
        ylabel,
        ha="left",
        va="center",
        rotation="vertical",
        fontsize=30,
    )
    plt.tight_layout(rect=[0.014, 0.025, 1, 0.92])
    plt.subplots_adjust(hspace=0.5, wspace=0.1)  # Increase the space between subplots
    axes[0].legend(
        loc="lower center",
        bbox_to_anchor=(0.98, 1.18),
        ncol=4,
        fancybox=True,
        fontsize=26,
    )


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


def plot_rob_pressure_runtimes():
    for axes, i, ax, df_id, unique_id, fig, xs, label in get_unique_datas(DATA):
        (
            df_runtime_no_prefetch,
            df_runtime_prefetch,
            df_runtime_no_prefetch_remote,
            df_runtime_prefetch_remote,
        ) = split_into_local_remote_prefetch_noPrefetch(df_id)
        data_df = pd.DataFrame(
            {
                "x": xs,
                "Local": df_runtime_no_prefetch["access_runtime"].values * 1000000000,
                "Local Prefetched": df_runtime_prefetch["access_runtime"].values
                * 1000000000,
                "Remote": df_runtime_no_prefetch_remote["access_runtime"].values
                * 1000000000,
                "Remote Prefetched": df_runtime_prefetch_remote["access_runtime"].values
                * 1000000000,
            }
        )
        ax.set_title(f"{label}", fontsize=26)
        if i == len(axes) - 1:
            ax.set_xlabel(" ")
        else:
            ax.set_xlabel(" ")
        ax.set_ylabel(" ")
        ax.set_yscale("log")
        for label, marker, lnstyle in zip(
            ["Local", "Local Prefetched", "Remote", "Remote Prefetched"],
            MARKERS,
            LINESTYLES,
        ):
            ax.plot(
                data_df["x"],
                data_df[label],
                label=label,
                marker=marker,
                linestyle=lnstyle,
            )
        ax.set_yscale("log")
        ax.set_xscale("log")
        ax.legend([], [], frameon=False)
        ax.set_xticks(xs)
        ax.set_xticklabels(xs)
        ax.xaxis.set_minor_locator(ticker.NullLocator())
        ax.set_yticks([10, 100])
        ax.set_yticklabels(["10", "100"])

        ax.set_xticks([2, 5, 11, 18, 31, 53, 90, 259, 741, 2115])
        ax.set_xlim([1.8, 2300])
        ax.grid(axis="y")

    add_fancy_labels_and_legend(fig, axes, "Runtime per Access [ns]")
    fig.savefig(f"{MT_FINAL_FIGURES_DIR}/rob_pressure.pdf")
    # plt.show()


def plot_rob_pressure():
    for axes, i, ax, df_id, unique_id, fig, xs, label in get_unique_datas(DATA):
        (
            df_runtime_no_prefetch,
            df_runtime_prefetch,
            df_runtime_no_prefetch_remote,
            df_runtime_prefetch_remote,
        ) = split_into_local_remote_prefetch_noPrefetch(df_id)
        speed_up = (
            df_runtime_no_prefetch["access_runtime"].values
            / df_runtime_prefetch["access_runtime"].values
        )
        speed_up_remote = (
            df_runtime_no_prefetch_remote["access_runtime"].values
            / df_runtime_prefetch_remote["access_runtime"].values
        )
        data_df = pd.DataFrame(
            {
                "x": xs,
                "Local": speed_up,
                "Remote": speed_up_remote,
            }
        )

        ax.set_title(f"{label}", fontsize=26)
        if i == len(axes) - 1:
            ax.set_xlabel(" ")
        else:
            ax.set_xlabel(" ")
        ax.set_ylabel(" ")
        for label, marker, lnstyle in zip(
            [
                "Local",
                "Remote",
            ],
            MARKERS[::2],
            LINESTYLES[::2],
        ):
            ax.plot(
                data_df["x"],
                data_df[label],
                label=label,
                marker=marker,
                linestyle=lnstyle,
            )
        ax.legend([], [], frameon=False)
        ax.set_xscale("log")
        ax.set_xticks(xs)
        ax.set_xticklabels(xs)
        ax.set_xticklabels(
            ax.get_xticklabels(),
        )
        ax.set_xticks([2, 5, 11, 18, 31, 53, 90, 259, 741, 2115])
        ax.xaxis.set_minor_locator(ticker.NullLocator())

        ax.grid(axis="y")

    add_fancy_labels_and_legend(fig, axes, "Speedup using Prefetching")

    fig.savefig(f"{MT_FINAL_FIGURES_DIR}/rob_pressure_speedup.pdf")
    # plt.show()


if __name__ == "__main__":
    plot_rob_pressure_runtimes()
    plot_rob_pressure()
