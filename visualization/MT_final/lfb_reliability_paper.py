import json
import os
import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np

from import_helper import *


plt.rcParams["xtick.labelsize"] = 17  # X tick label font size
plt.rcParams["ytick.labelsize"] = 17  # Y tick label font size

LOCALITIES = ["NTA"]
UNIQUE_IDS = ["AMD1", "INTEL1", "AMD2", "INTEL2", "ARM2", "INTEL3"]


def plot_lfb_full_behavior_vary_locality():
    df = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/lfb_reliability_final")
    ca_df = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/lfb_reliability_ca")
    df["config::num_resolves"] = df["config::num_resolves"].astype(int)

    unique_ids = UNIQUE_IDS
    unique_localities = df["config::locality_hint"].unique()

    print(df)
    print(unique_localities)
    for unique_locality in LOCALITIES:
        num_plots = int(np.ceil(len(unique_ids) / 2)) * 2 + 2
        fig, axes = plt.subplots(
            int(np.ceil(num_plots / 2)), 2, figsize=(10, 7.35), sharex=True
        )
        if len(unique_ids) == 1:
            axes = [axes]
        else:
            axes = axes.ravel()

        # share y axis among weak and strong prefetching cpus
        for i in range(2, len(axes), 2):
            axes[i].sharey(axes[i - 2])
        for i in range(3, len(axes), 2):
            axes[i].sharey(axes[i - 2])

        unique_ids = list(unique_ids)
        unique_ids.sort(key=lambda x: (int(x[-1]), x))
        for i, (ax, unique_id) in enumerate(zip(axes, UNIQUE_IDS)):
            df_id = df[
                (df["id"] == unique_id)
                & (df["config::locality_hint"] == unique_locality)
                #    & (df["config::prefetch"] == True)
                & (df["config::num_resolves"] <= 32)
            ].sort_values(by=["config::num_resolves"])

            total_runtime = (
                df_id["median_access_runtime"] + df_id["median_prefetch_runtime"]
            )
            df_id["rel_access_runtime"] = df_id["median_access_runtime"] / total_runtime
            # ax.set_title(f"{unique_id}", x=0.46 if i == 6 else 0.5)
            ax.text(
                16,
                900 if i % 2 else 3000,
                f"{NODEID_TO_LABEL[unique_id]}",
                ha="center",
                va="top",
                fontsize=21,
                bbox=dict(facecolor="white", edgecolor="none", boxstyle="round,pad=0"),
            )
            if i == len(axes) - 1:
                ax.set_xlabel(" ")
            else:
                ax.set_xlabel(" ")
            ax.set_ylabel(" ")

            # Plot the stacked area chart
            ax.fill_between(
                df_id["config::num_resolves"],
                0,
                (df_id["median_prefetch_runtime"])
                * df_id["config::num_resolves"]
                * 1000000000,
                alpha=0.8,
                label="Prefetch Runtime",
                zorder=3,
            )
            ax.fill_between(
                df_id["config::num_resolves"],
                (df_id["median_prefetch_runtime"])
                * df_id["config::num_resolves"]
                * 1000000000,
                (df_id["median_prefetch_runtime"] + df_id["median_access_runtime"])
                * df_id["config::num_resolves"]
                * 1000000000,
                alpha=0.8,
                label="Access Runtime",
                zorder=3,
            )

            # ax.set_yscale("log")

        ### add ca plots

        ca_df = ca_df[
            (ca_df["config::locality_hint"] == unique_locality)
            & (ca_df["config::num_resolves"] <= 32)
            & (ca_df["perf::BR_MIS_PRED"].isna())
        ].sort_values(by=["config::num_resolves"])

        total_runtime = (
            ca_df["median_access_runtime"] + ca_df["median_prefetch_runtime"]
        )
        ca_df["rel_access_runtime"] = ca_df["median_access_runtime"] / total_runtime

        ca_df_weak = ca_df[ca_df["config::reliability"] == True]
        ca_df_strong = ca_df[ca_df["config::reliability"] == False]
        for ax, df in zip([axes[6], axes[7]], [ca_df_weak, ca_df_strong]):
            ax.fill_between(
                df["config::num_resolves"],
                0,
                (df["median_prefetch_runtime"])
                * df["config::num_resolves"]
                * 1000000000,
                alpha=0.8,
                label="Prefetch Runtime",
                zorder=3,
            )
            ax.fill_between(
                df["config::num_resolves"],
                (df["median_prefetch_runtime"])
                * df["config::num_resolves"]
                * 1000000000,
                (df["median_prefetch_runtime"] + df["median_access_runtime"])
                * df["config::num_resolves"]
                * 1000000000,
                alpha=0.8,
                label="Access Runtime",
                zorder=3,
            )
            ax.legend([], [], frameon=False)
            ax.grid(True)

        fig.text(
            0.5,
            0.02,
            "Batch Size",
            ha="center",
            va="center",
            fontsize=24,
        )

        plt.tight_layout(rect=[-0.02, 0.02, 1, 0.92])
        plt.subplots_adjust(hspace=0.2)  # Increase the space between subplots
        plt.subplots_adjust(wspace=0.1)  # Increase the space between subplots
        fig.text(
            0.03,
            0.5,
            "Runtime [ms]",
            ha="center",
            va="center",
            rotation="vertical",
            fontsize=24,
        )
        # move_axis_right(axes[6], 0.25)
        axes[6].set_xlabel(" ")
        axes[7].set_xlabel(" ")
        axes[6].set_ylabel(" ")
        axes[7].set_ylabel(" ")
        ax
        axes[6].text(
            16,
            3000,
            f"A64FX-Weak",
            ha="center",
            va="top",
            fontsize=21,
            bbox=dict(facecolor="white", edgecolor="none", boxstyle="round,pad=0"),
        )
        axes[7].text(
            16,
            900,
            f"A64FX-Strong",
            ha="center",
            va="top",
            fontsize=21,
            bbox=dict(facecolor="white", edgecolor="none", boxstyle="round,pad=0"),
        )
        for ax in axes:
            ax.legend([], [], frameon=False)
            ax.grid(True)
            ax.grid(axis="y")
            ax.set_xlabel("")
            ax.set_xlim([1, 32])
            ax.spines["top"].set_linewidth(1)
            ax.spines["top"].set_edgecolor("lightgray")
            ax.spines["top"].set_linestyle((0, (12, 12)))
            ax.spines["right"].set_linewidth(1)
            ax.spines["right"].set_edgecolor("lightgray")
            # axes[6].set_xticks(range(1, 32, 2), ["" for _ in range(1, 32, 2)])
            ax.spines["right"].set_linestyle((0, (12, 12)))
        axes[6].set_xticks(range(1, 32, 4), range(1, 32, 4))
        axes[7].set_xticks(range(1, 32, 4), range(1, 32, 4))

        for ax in axes:
            ax.grid(which="minor", axis="x")
            ax.set_xticks(range(1, 32, 2), minor=True)

            ax.tick_params(axis="x", which="minor", length=2)

        for ax in axes[::2]:
            ax.set_yticks([0, 1000, 2000, 3000], [0, 1, 2, 3])
            ax.set_ylim([0, 3200])
        for ax in axes[1::2]:
            ax.set_yticks(
                [0, 300, 600, 900],
                ["0", ".3", ".6", ".9"],
            )
            ax.set_ylim([0, 950])
        axes[0].legend(
            loc="lower center",
            bbox_to_anchor=(1.04, 0.99),
            ncol=3,
            fancybox=True,
            fontsize=24,
        )
        plt.show()
        fig.savefig(f"{MT_FINAL_FIGURES_DIR}/lfb_reliability_absolute_paper.pdf")

        # Loop through each unique id and plot
        ##for unique_locality in unique_localities:
        ##    df_locality = df[df["config::locality_hint"] == unique_locality]
        ##    fig, axes = plt.subplots(len(unique_ids), 1, figsize=(12, 8 * len(unique_ids)))


if __name__ == "__main__":
    plot_lfb_full_behavior_vary_locality()
