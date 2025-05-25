import json
import os
import matplotlib.pyplot as plt
import seaborn as sns
from matplotlib.ticker import FormatStrFormatter

from import_helper import *

plt.rcParams["xtick.labelsize"] = 20  # X tick label font size
plt.rcParams["ytick.labelsize"] = 20  # Y tick label font size

RUNTIME_DEFINITIONS = {
    "Upper": "upper_runtime",
    "Prefetched": "runtime",
    "Lower": "baseline_runtime",
}
UNIQUE_IDS = ["AMD1", "AMD2", "INTEL1", "ARM1", "INTEL2", "ARM2", "INTEL3"]

MARKERS = {
    "NTA": "x",
    "T0": "o",
    "T1": "*",
    "T2": "|",
}
KiB = 2**10
MiB = 2**20
CACHE_SIZE = {
    "AMD1": [32 * KiB, 512 * KiB, 16 * MiB],
    "AMD2": [32 * KiB, 512 * KiB, 32 * MiB],
    "INTEL1": [32 * KiB, 256 * KiB, 20 * 2.5 * MiB],
    "INTEL2": [32 * KiB, 1 * MiB, 18 * 1.4 * MiB],
    "INTEL3": [48 * KiB, 1.3 * MiB, 48 * MiB],
    "ARM1": [64 * KiB, 8 * MiB, None],
    "ARM2": [64 * KiB, 1 * MiB, 111.3 * MiB],
}

def plot_prefetch_locality_behavior():
    df = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/prefetch_locality_behavior_grace"
    )
    print(df)

    unique_ids = df["id"].unique()
    uniques_locality_hint = df["config::locality_hint"].unique()

    print(uniques_locality_hint)
    fig, axes = plt.subplots(4, 2, figsize=(12, 9), sharex=True, sharey=True)
    axes = axes.flatten()
    axes[1].axis("off")
    axes = [axes[0]] + list(axes[2:])
    if len(unique_ids) == 1:
        axes = [axes]

    for i, (ax, unique_id) in enumerate(zip(axes, UNIQUE_IDS)):
        for name, cache in zip(["L1", "L2", "L3"], CACHE_SIZE[unique_id]):
            if cache is None:
                continue
            cachelinesize = 256 if unique_id == "ARM1" else 64
            ax.text(
                x=cache / (cachelinesize) / 1.2,
                y=11 if name == "L3" else 3.1,
                s=name,
                ha="center",
                va="bottom",
                color="dimgray",
                fontsize=20,
                bbox=dict(facecolor="white", edgecolor="none", boxstyle="round,pad=0"),
            )
            ax.axvline(
                x=cache / (cachelinesize * 2),
                color="dimgray",
                linestyle="--",
                linewidth=1.5,
            )
        df_id = df[
            (df["id"] == unique_id)
            & (df["config::bind_prefetch_to_memory_load"] == True)
            & (df["config::madvise_huge_pages"] == True)
            & (df["config::num_resolves_per_measure"] >= 32)
            & (df["config::num_resolves_per_measure"] <= 7000000)
        ]
        for label, runtime_kind in RUNTIME_DEFINITIONS.items():

            if label == "Prefetched":
                for unique_locality_hint in uniques_locality_hint:

                    df_final = df_id[
                        df_id["config::locality_hint"] == unique_locality_hint
                    ].sort_values(by="config::num_resolves_per_measure")
                    ax.plot(
                        df_final["config::num_resolves_per_measure"],
                        df_final[runtime_kind] * 1000000000,
                        marker=MARKERS[unique_locality_hint],
                        label=f"{unique_locality_hint}",
                        linewidth=1.5,
                        markersize=12 if unique_locality_hint == "T2" else 7,
                    )
            else:
                df_final = df_id[
                    df_id["config::locality_hint"] == uniques_locality_hint[0]
                ].sort_values(by="config::num_resolves_per_measure")
                ax.plot(
                    df_final["config::num_resolves_per_measure"],
                    df_final[runtime_kind] * 1000000000,
                    marker="",
                    linewidth=1.5,
                    linestyle="dotted",
                    label=label,
                )
            # ax.text(
            #    1,
            #    0.5,
            #    f"{NODEID_TO_LABEL[unique_id]}",
            #    transform=ax.transAxes,
            #    ha="left",
            #    va="center",
            #    rotation=270,
            #    fontsize=23,
            # )
            ax.text(
                10e5,
                5,
                f"{NODEID_TO_LABEL[unique_id]}",
                ha="center",
                va="center",
                fontsize=25,
                bbox=dict(facecolor="white", edgecolor="none", boxstyle="round,pad=0"),
            )
            if i == 3:
                ax.set_ylabel((" " * 16) + "Access Latency [ns]", fontsize=27)
            else:
                ax.set_ylabel("")
            ax.set_yscale("log")
            ax.set_xscale("log")
            if unique_id in ["ARM2", "INTEL3"]:
                if unique_id == "ARM2":
                    ax.set_xlabel(
                        (" " * 38) + "Random Cache Line Accesses", fontsize=27
                    )
            else:
                ax.set_xticks(
                    [1, 10, 100, 1000, 10000, 100000, 1000000, 10000000],
                    ["", "", "", "", "", "", "", ""],
                )
            ax.yaxis.set_major_formatter(FormatStrFormatter("%.0f"))
            current_ylim = ax.get_ylim()
            # ax.set_ylim(current_ylim[0], max(current_ylim[1], 140.0))
            # if unique_id == "ARM2":
            #    ax.set_ylim(current_ylim[0], max(current_ylim[1], 240.0))
            ax.grid(True)
            ax.spines["top"].set_linewidth(1)
            ax.spines["top"].set_edgecolor("lightgray")
            ax.spines["top"].set_linestyle((0, (12, 12)))
            ax.spines["right"].set_linewidth(1)
            ax.spines["right"].set_edgecolor("lightgray")
            ax.spines["right"].set_linestyle((0, (12, 12)))

    plt.tight_layout(rect=[0, 0, 1, 1])
    plt.subplots_adjust(hspace=0.07, wspace=0.03)
    axes[0].legend(
        loc="center left",
        bbox_to_anchor=(1.03, 0.5),
        ncol=2,
        fancybox=True,
        fontsize=25,
    )
    plt.savefig(
        f"{MT_FINAL_FIGURES_DIR}/prefetch_locality_behavior_two_col_names_on_plot.pdf"
    )

    plt.show()


if __name__ == "__main__":
    plot_prefetch_locality_behavior()
