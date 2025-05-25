import json
import os
import matplotlib.pyplot as plt
import seaborn as sns
from matplotlib.ticker import FormatStrFormatter

from import_helper import *

plt.rcParams["xtick.labelsize"] = 20  # X tick label font size
plt.rcParams["ytick.labelsize"] = 20  # Y tick label font size

RUNTIME_DEFINITIONS = {
    "Lower": "baseline_runtime",
    "Prefetched": "runtime",
    "Upper": "upper_runtime",
}

MARKERS = {
    "NTA": "x",
    "T0": "o",
    "T1": "*",
    "T2": "|",
}


def plot_prefetch_locality_behavior():
    df = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/prefetch_locality_behavior_grace"
    )
    print(df)

    unique_ids = df["id"].unique()
    uniques_locality_hint = df["config::locality_hint"].unique()

    print(uniques_locality_hint)
    fig, axes = plt.subplots(len(unique_ids), 1, figsize=(12, 15.5), sharex=True)

    if len(unique_ids) == 1:
        axes = [axes]

    for i, (ax, unique_id) in enumerate(zip(axes, UNIQUE_IDS)):
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
            ax.text(
                1,
                0.5,
                f"{unique_id}",
                transform=ax.transAxes,
                ha="left",
                va="center",
                rotation=270,
                fontsize=21,
            )
            if i == int(len(unique_ids) / 2):
                ax.set_ylabel("Access Latency [ns]", fontsize=27)
            else:
                ax.set_ylabel("")
            ax.set_yscale("log")
            ax.set_xscale("log")
            if i == len(axes) - 1:
                ax.set_xlabel("Access Array Size", fontsize=27)
            else:
                ax.set_xticks(
                    [1, 10, 100, 1000, 10000, 100000, 1000000, 10000000],
                    ["", "", "", "", "", "", "", ""],
                )
            ax.yaxis.set_major_formatter(FormatStrFormatter("%.0f"))
            current_ylim = ax.get_ylim()
            ax.set_ylim(current_ylim[0], max(current_ylim[1], 140.0))
            if unique_id == "ARM2":
                ax.set_ylim(current_ylim[0], max(current_ylim[1], 240.0))

            if i == 0:
                ax.legend(
                    loc="lower center",
                    bbox_to_anchor=(0.5, 1.1),
                    ncol=6,
                    fancybox=True,
                    fontsize=21,
                )
            ax.grid(True)
            ax.spines["top"].set_linewidth(1)
            ax.spines["top"].set_edgecolor("lightgray")
            ax.spines["top"].set_linestyle((0, (12, 12)))
            ax.spines["right"].set_linewidth(1)
            ax.spines["right"].set_edgecolor("lightgray")
            ax.spines["right"].set_linestyle((0, (12, 12)))

    plt.tight_layout(rect=[0, 0.01, 1, 0.99])
    plt.subplots_adjust(hspace=0.15)
    plt.savefig(f"{MT_FINAL_FIGURES_DIR}/prefetch_locality_behavior.pdf")

    plt.show()


if __name__ == "__main__":
    plot_prefetch_locality_behavior()
