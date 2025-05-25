import json
import os
import matplotlib.pyplot as plt
import seaborn as sns


from helper import *


RUNTIME_DEFINITIONS = {
    "Cached": "lower_runtime",
    "Prefetched": "runtime",
    "DRAM": "upper_runtime",
}


def plot_lfb_full_behavior(run):
    df = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/{run}")
    print(df)
    df["config.num_prefetched"] = df["config.num_prefetched"].astype(int)

    unique_ids = df["id"].unique()

    fig, axes = plt.subplots(len(unique_ids), 1, figsize=(12, 8 * len(unique_ids)))

    if len(unique_ids) == 1:
        axes = [axes]

    # Loop through each unique id and plot
    for i, (ax, unique_id) in enumerate(zip(axes, unique_ids)):
        df_id = df[df["id"] == unique_id]
        for label, runtime_kind in RUNTIME_DEFINITIONS.items():

            df_id = df_id.sort_values(by="config.num_prefetched")

            ax.plot(
                df_id["config.num_prefetched"],
                df_id[runtime_kind],
                marker="o",
                label=label,
            )
            ax.set_title(f"{unique_id}")
            if i == len(axes) - 1:
                ax.set_xlabel("Number of random accesses")
            ax.set_ylabel("Load Latency")
            ax.set_yscale("log")
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


def plot_lfb_full_behavior_vary_measure():
    df = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/lfb_full_behavior_vary_measure_size"
    )
    print(df)
    df["config.num_prefetched"] = df["config.num_prefetched"].astype(int)

    unique_ids = df["id"].unique()

    fig, axes = plt.subplots(len(unique_ids), 1, figsize=(12, 8 * len(unique_ids)))

    if len(unique_ids) == 1:
        axes = [axes]

    # Loop through each unique id and plot
    for i, (ax, unique_id) in enumerate(zip(axes, unique_ids)):
        df_id = df[df["id"] == unique_id]
        for label, runtime_kind in RUNTIME_DEFINITIONS.items():

            df_id = df_id.sort_values(by="config.measure_until")

            ax.plot(
                df_id["config.measure_until"],
                df_id[runtime_kind],
                marker="o",
                label=label,
            )
            ax.set_title(f"{unique_id}")

            if i == len(axes) - 1:
                ax.set_xlabel("Number of Measured Resolves")
            ax.set_ylabel("Measured Part of Runtime (s)")
            ax.set_yscale("log")
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


def plot_lfb_full_behavior_vary_locality():
    df = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/lfb_full_behavior_vary_locality_hints_repeat"
    )
    print(df)
    df["config.num_prefetched"] = df["config.num_prefetched"].astype(int)

    unique_ids = df["id"].unique()
    unique_localities = df["config.locality_hint"].unique()

    print(unique_localities)
    fig, axes = plt.subplots(len(unique_ids), 1, figsize=(12, 8 * len(unique_ids)))

    if len(unique_ids) == 1:
        axes = [axes]
    for i, (ax, unique_id) in enumerate(zip(axes, unique_ids)):
        df_id = df[df["id"] == unique_id]
        for label, runtime_kind in RUNTIME_DEFINITIONS.items():

            if label == "Prefetched":
                for unique_locality in unique_localities:

                    df_final = df_id[
                        df_id["config.locality_hint"] == unique_locality
                    ].sort_values(by="config.num_prefetched")
                    ax.plot(
                        df_final["config.num_prefetched"],
                        df_final[runtime_kind],
                        marker="o",
                        label=f"{label} - {unique_locality}",
                    )
            else:
                df_final = df_id[
                    df_id["config.locality_hint"] == unique_localities[0]
                ].sort_values(by="config.num_prefetched")
                ax.plot(
                    df_final["config.num_prefetched"],
                    df_final[runtime_kind],
                    marker="o",
                    label=label,
                )
            ax.set_title(f"{unique_id}")
            if i == len(axes) - 1:
                ax.set_xlabel("Number of random accesses")
            ax.set_ylabel("Load Latency")
            ax.set_yscale("log")
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
    # Loop through each unique id and plot
    ##for unique_locality in unique_localities:
    ##    df_locality = df[df["config.locality_hint"] == unique_locality]
    ##    fig, axes = plt.subplots(len(unique_ids), 1, figsize=(12, 8 * len(unique_ids)))


if __name__ == "__main__":
    plot_lfb_full_behavior_vary_locality()

    plot_lfb_full_behavior("lfb_full_behavior")
    plot_lfb_full_behavior_vary_measure()
