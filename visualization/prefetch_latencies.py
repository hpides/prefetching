import json
import matplotlib.pyplot as plt
import os
import numpy as np


class LatencySeries:
    data: []
    name: str

    def __init__(self, data, name):
        self.data = data
        self.name = name


def read_benchmark_file(json_path: str):
    with open(json_path, "r") as fp:
        benchmark = json.load(fp)
    return benchmark


def plot_hist(latency_series_left: LatencySeries, latency_series_right: LatencySeries):
    combined_data = np.concatenate(
        (latency_series_left.data, latency_series_right.data)
    )
    bin_edges = np.histogram_bin_edges(combined_data, bins=20)
    # Compute histograms for both series using common bin edges
    left_counts, _ = np.histogram(latency_series_left.data, bins=bin_edges)
    right_counts, _ = np.histogram(latency_series_right.data, bins=bin_edges)

    # Plot the histograms
    plt.stairs(left_counts, bin_edges, label=latency_series_left.name)
    plt.stairs(right_counts, bin_edges, label=latency_series_right.name)

    # Add labels and legend
    plt.title("Prefetch Latencies uncached vs. cached")
    plt.xlabel("Latency (cycles)")
    plt.ylabel("Frequency")
    plt.legend()
    plt.show()


if __name__ == "__main__":
    prefetch_latencies_benchmark = read_benchmark_file(
        os.path.join(
            os.path.dirname(__file__), os.pardir, "data", "prefetch_latencies_1GiB.json"
        )
    )
    plot_hist(
        LatencySeries(
            prefetch_latencies_benchmark["latencies_cached_prefetch"],
            "latencies_cached_prefetch",
        ),
        LatencySeries(
            prefetch_latencies_benchmark["latencies_uncached_prefetch"],
            "latencies_uncached_prefetch",
        ),
    )
    print(prefetch_latencies_benchmark)
