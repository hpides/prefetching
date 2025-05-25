import json
import os
import matplotlib.pyplot as plt
import seaborn as sns


from helper import *


RUNTIME_DEFINITIONS = {
    "Baseline (load)": "baseline_runtime",
    "Prefetched": "runtime",
    "Upper bound (no prior load/prefetch)": "upper_runtime",
}


def plot_prefetch_locality_behavior():
    df = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/prefetch_locality_behavior_long"
    )
    df = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/prefetch_locality_behavior_high_res"
    )

    print(df)

    unique_ids = df["id"].unique()
    uniques_locality_hint = df["config.locality_hint"].unique()

    print(uniques_locality_hint)
    fig, axes = plt.subplots(len(unique_ids), 1, figsize=(12, 8 * len(unique_ids)))

    if len(unique_ids) == 1:
        axes = [axes]
    for i, (ax, unique_id) in enumerate(zip(axes, unique_ids)):
        df_id = df[
            (df["id"] == unique_id)
            & (df["config.bind_prefetch_to_memory_load"] == True)
        ]
        for label, runtime_kind in RUNTIME_DEFINITIONS.items():

            if label == "Prefetched":
                for unique_locality_hint in uniques_locality_hint:

                    df_final = df_id[
                        df_id["config.locality_hint"] == unique_locality_hint
                    ].sort_values(by="config.num_resolves_per_measure")
                    ax.plot(
                        df_final["config.num_resolves_per_measure"],
                        df_final[runtime_kind],
                        marker="o",
                        label=f"{label} - {unique_locality_hint}",
                    )
            else:
                df_final = df_id[
                    df_id["config.locality_hint"] == uniques_locality_hint[0]
                ].sort_values(by="config.num_resolves_per_measure")
                ax.plot(
                    df_final["config.num_resolves_per_measure"],
                    df_final[runtime_kind],
                    marker="o",
                    label=label,
                )
            ax.set_title(f"{unique_id}")
            if i == len(axes) - 1:
                ax.set_xlabel("Number of random accesses")
            ax.set_ylabel("Runtime")
            ax.set_yscale("log")
            ax.set_xscale("log")
            if i == 0:
                ax.legend(
                    loc="lower center",
                    bbox_to_anchor=(0.5, 1.2),
                    ncol=3,
                    fancybox=True,
                    shadow=True,
                )
            ax.grid(True)

    # plt.tight_layout()
    plt.subplots_adjust(hspace=0.3)  # Increase the space between subplots
    plt.show()


if __name__ == "__main__":
    plot_prefetch_locality_behavior()
