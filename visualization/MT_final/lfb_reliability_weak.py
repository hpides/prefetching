import json
import os
import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np

from import_helper import *

LOCALITIES = ["NTA"]
UNIQUE_IDS = ["AMD1", "INTEL1", "AMD2", "INTEL2", "ARM2", "INTEL3"]

plt.rcParams["xtick.labelsize"] = 19  # X tick label font size
plt.rcParams["ytick.labelsize"] = 19  # Y tick label font size


def plot_data_on_axis(df_id, ax, name):
    df_id = df_id[
        (df_id["config::measure_until"] <= 32) & (df_id["config::measure_until"] >= 4)
    ]
    df_id["runtime"] = (
        df_id["runtime"]
        / (df_id["config::num_repetitions"] / df_id["config::num_prefetched"])
        / df_id["config::measure_until"]
    ) * 1000000000
    df_id["lower_runtime"] = (
        df_id["lower_runtime"]
        / (df_id["config::num_repetitions"] / df_id["config::num_prefetched"])
        / df_id["config::measure_until"]
    ) * 1000000000
    df_id["upper_runtime"] = (
        df_id["upper_runtime"]
        / (df_id["config::num_repetitions"] / df_id["config::num_prefetched"])
        / df_id["config::measure_until"]
    ) * 1000000000
    ax.set_title(f"{name}", x=0.5, fontsize=22)

    ax.set_xlabel(" ")
    ax.set_ylabel(" ")

    ax.plot(
        df_id["config::measure_until"],
        df_id["lower_runtime"],
        label="Lower Runtime",
        zorder=4,
        linestyle="--",
        linewidth=2,
    )
    ax.plot(
        df_id["config::measure_until"],
        df_id["runtime"],
        label="Prefetched",
        marker="x",
        zorder=4,
        linewidth=2,
    )
    ax.plot(
        df_id["config::measure_until"],
        df_id["upper_runtime"],
        label="Upper Runtime",
        linestyle="--",
        zorder=4,
        linewidth=2,
    )

    ax.set_yscale("log")

    ax.legend([], [], frameon=False)
    ax.set_xticks(range(5, 33, 5), range(5, 33, 5))
    print(ax.get_yticklabels())
    ax.set_yticks([10, 100])
    ax.set_yticklabels(["10", "100"])
    ax.grid(True)
    ax.spines["top"].set_linewidth(1)
    ax.spines["top"].set_edgecolor("lightgray")
    ax.spines["top"].set_linestyle((0, (12, 12)))
    ax.spines["right"].set_linewidth(1)
    ax.spines["right"].set_edgecolor("lightgray")
    ax.spines["right"].set_linestyle((0, (12, 12)))


def plot_reliability_weak_lfb_size():
    df = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/lfb_full_behavior_weak_final2"
    )
    ca_df = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/lfb_full_behavior_weak_arm1"
    )
    df["config::measure_until"] = df["config::measure_until"].astype(int)

    unique_ids = UNIQUE_IDS
    unique_localities = df["config::locality_hint"].unique()

    print(df)
    print(unique_localities)
    for unique_locality in LOCALITIES:
        num_plots = int(np.ceil(len(unique_ids) / 2)) * 2 + 2
        fig, axes = plt.subplots(
            int(np.ceil(num_plots / 2)),
            2,
            figsize=(15, 20),
            sharey=True,
        )

        if len(unique_ids) == 1:
            axes = [axes]
        else:
            axes = axes.ravel()

        unique_ids = list(unique_ids)
        unique_ids.sort(key=lambda x: (int(x[-1]), x))
        for i, (ax, unique_id) in enumerate(zip(axes, UNIQUE_IDS)):
            if unique_id not in unique_ids:
                print(f"{unique_id} not found in benchmark data")
                continue
            df_id = df[
                (df["id"] == unique_id)
                & (df["config::locality_hint"] == unique_locality)
            ].sort_values(by=["config::measure_until"])

            plot_data_on_axis(df_id, ax, unique_id)
        for name, reliability, ax in zip(
            ["ARM1 - Weak", "ARM1 - Strong"], [True, False], [axes[6], axes[7]]
        ):
            df_id = ca_df[
                (ca_df["config::locality_hint"] == unique_locality)
                & (ca_df["config::profile"] == False)
                & (ca_df["config::reliability"] == reliability)
            ].sort_values(by=["config::measure_until"])
            plot_data_on_axis(df_id, ax, name)
        fig.text(
            0.5,
            0.02,
            "Number of Measured Accesses",
            ha="center",
            va="center",
            fontsize=27,
        )
        fig.text(
            0.03,
            0.5,
            "Latency per Access [ns]",
            ha="center",
            va="center",
            rotation="vertical",
            fontsize=27,
        )
        plt.tight_layout(rect=[0.02, 0.06, 1, 0.90])
        plt.subplots_adjust(hspace=0.5)  # Increase the space between subplots
        axes[0].legend(
            loc="lower center",
            bbox_to_anchor=(1.0, 1.2),
            ncol=3,
            fancybox=True,
            shadow=False,
            fontsize=23,
        )
        plt.show()
        fig.savefig(f"{MT_FINAL_FIGURES_DIR}/lfb_reliability_weak.pdf")


if __name__ == "__main__":
    plot_reliability_weak_lfb_size()
