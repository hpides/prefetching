import json
import os
import matplotlib.pyplot as plt
import seaborn as sns


from helper import *


ids = ["amd", "intel", "local"]
filenames_interleaving = {
    "amd": "lfb_size_simple_interleaving_amd.json",
    "intel": "lfb_size_simple_interleaving_intel.json",
    "local": "lfb_size_simple_interleaving_local.json",
}

lfbs = {
    "amd": 22.0,
    "intel": 10.0,
    "local": 22.0,
}

def plot_interleaved():
    for id in ids:
        lfb_bench = import_benchmark(
            os.path.join("lfb_interleaved", filenames_interleaving[id])
        )
        results = lfb_bench["results"]
        results = sorted(
            results, key=lambda result: result["config"]["prefetch_distance"]
        )
        prefetching_distances = [
            int(result["config"]["prefetch_distance"]) for result in results
        ]
        runtime = [result["runtime"] for result in results]

        pointplot = sns.pointplot(x=prefetching_distances, y=runtime, label=id)
        # color = pointplot.get_lines()[0].get_color()  # Get the color of the pointplot
        # sns.lineplot(x=[10, 10], y=[0, 0.3], color=color)

    plt.title("LFB Size Benchmark - Interleaved")
    plt.xlabel("Prefetch Distance")
    plt.ylabel("Runtime (s)")

    plt.show()


ids = ["amd", "intel", "local"]
filenames_batched = {
    "amd": "lfb_size_batched_amd.json",
    "intel": "lfb_size_batched_intel.json",
    "local": "lfb_size_batched_local.json",
}


def plot_batched():
    for id in ids:
        lfb_bench = import_benchmark(os.path.join("lfb_batched", filenames_batched[id]))
        results = lfb_bench["results"]
        results = sorted(results, key=lambda result: result["config"]["batch_size"])
        prefetching_distances = [result["config"]["batch_size"] for result in results]
        runtime = [result["runtime"] for result in results]
        runtime = [p[0] * p[1] for p in zip(runtime, prefetching_distances)]

        sns.pointplot(x=prefetching_distances, y=runtime, label=id)
    plt.title("LFB Size Benchmark - Batched")
    plt.xlabel("Batch Size")
    plt.ylabel("Runtime (s)")
    # plt.yscale("log")
    plt.show()


pc_ids = ["pc_local.json", "pc_amd.json", "pc_intel.json"]
def plot_pc():
    for id in pc_ids:
        lfb_bench = import_benchmark(os.path.join("pc_benchmark", id))
        results = lfb_bench["results"]
        results = sorted(
            results, key=lambda result: result["config"]["num_parallel_pc"]
        )
        num_parallel_pc = [result["config"]["num_parallel_pc"] for result in results]
        runtime = [result["runtime"] for result in results]
        if id == "pc_intel.json":
            runtime = [p[0] / p[1] for p in zip(runtime, num_parallel_pc)]

        sns.pointplot(x=num_parallel_pc, y=runtime, label=id)
    plt.title("LFB Size Benchmark - Pointer Chasing")
    plt.xlabel("Parallel Pointer Chases")
    plt.ylabel("Runtime (s)")
    # plt.yscale("log")
    plt.show()


def plot_pc_access_size():
    df = load_results_benchmark_directory_to_pandas(
        f"{DATA_DIR}/pointer_chase_prefetch"
    )
    print(df)
    df["config.num_parallel_pc"] = df["config.num_parallel_pc"].astype(int)
    df["runtime"] = df["median_runtime"].astype(float)

    unique_ids = df["id"].unique()
    unique_prefetch = df["config.prefetch"].unique()

    fig, axes = plt.subplots(len(unique_ids), 1, figsize=(12, 8 * len(unique_ids)))

    if len(unique_ids) == 1:
        axes = [axes]

    # Loop through each unique id and plot
    for ax, unique_id in zip(axes, unique_ids):
        for prefetch_setting in unique_prefetch:
            # Filter the DataFrame for the current id
            df_id = df[df["id"] == unique_id]
            df_id = df_id[df_id["config.prefetch"] == prefetch_setting]

            df_id = df_id.sort_values(by="config.num_parallel_pc")
            # ax.plot(
            #    df_id["config.num_parallel_pc"],
            #    df_id["runtime"],
            #    marker="o",
            # )

            df_id["relative_diff"] = -df_id["runtime"].pct_change()
            # if prefetch_setting:
            #    top_10_smallest = df_id.nsmallest(10, "runtime")
            #    ax.scatter(
            #        top_10_smallest["config.num_parallel_pc"],
            #        top_10_smallest["runtime"],
            #        color="red",
            #        s=100,
            #        zorder=5,
            #        label="Top 10 Smallest Runtimes",
            #    )
            ax.plot(
                df_id["config.num_parallel_pc"],
                df_id["relative_diff"],
                marker="o",
                label=("no " if prefetch_setting == "true" else "") + "prefetching",
            )
            ax.set_title(f"Performance for {unique_id}")
            ax.set_xlabel("Number of Parallel PCs")
            ax.set_ylabel("Runtime")
            ax.legend()
            ax.grid(True)

    plt.tight_layout()
    plt.subplots_adjust(hspace=0.3)  # Increase the space between subplots
    plt.show()


def plot_pc_access_size_old():
    df = load_results_benchmark_directory_to_pandas(f"{DATA_DIR}/lfb_size")
    df["config.prefetch_distance"] = df["config.prefetch_distance"].astype(int)
    df["runtime"] = df["runtime"].astype(float)

    unique_ids = df["id"].unique()

    fig, axes = plt.subplots(len(unique_ids), 1, figsize=(12, 8 * len(unique_ids)))

    if len(unique_ids) == 1:
        axes = [axes]

    # Loop through each unique id and plot
    for i, (ax, unique_id) in enumerate(zip(axes, unique_ids)):
        # Filter the DataFrame for the current id
        df_id = df[df["id"] == unique_id]
        df_id = df_id.sort_values(by="config.prefetch_distance")
        df_id["relative_diff"] = -df_id["runtime"].pct_change()

        ax.plot(
            df_id["config.prefetch_distance"],
            df_id["runtime"],
            marker="o",
            label="Runtime",
        )
        ax.set_ylabel("Runtime")
        ax.tick_params(axis="y")

        # Create a secondary y-axis
        ax2 = ax.twinx()
        ax2.plot(
            df_id["config.prefetch_distance"],
            df_id["relative_diff"] * 100,
            marker="o",
            color="r",
            label="Relative Difference",
        )
        ax2.set_ylabel("Relative Difference", color="r")
        ax2.tick_params(axis="y", labelcolor="r")

        ax.set_title(f"Prefetched Batched Loads on {unique_id}")
        if i == len(axes) - 1:
            ax.set_xlabel("Prefetch Distance")
        ax.grid(True)

        # Combine legends from both axes
        lines, labels = ax.get_legend_handles_labels()
        lines2, labels2 = ax2.get_legend_handles_labels()
        ax.legend(lines + lines2, labels + labels2, loc="upper right")

    plt.title(" ")
    # plt.tight_layout()
    plt.subplots_adjust(hspace=0.15)  # Increase the space between subplots
    plt.show()


if __name__ == "__main__":
    plot_pc_access_size_old()
    plot_pc_access_size()
    # plot_pc()
    # plot_interleaved()
    # plot_batched()
