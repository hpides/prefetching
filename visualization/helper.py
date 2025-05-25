import os
import json
import pandas as pd
import copy
import matplotlib.pyplot as plt

DATA_DIR = os.path.join(os.path.dirname(__file__), "..", "data")
MT_FINAL_FIGURES_DIR = os.path.join(os.path.dirname(__file__), "MT_final", "figures")

def import_benchmark(name):
    benchmark = {}
    with open(os.path.join(DATA_DIR, name), "r") as fp:
        benchmark = json.load(fp)
    return benchmark


def load_flat_jsons(directory):
    all_results = []
    for filename in os.listdir(directory):
        if filename.endswith(".json"):
            filepath = os.path.join(directory, filename)
            data = import_benchmark(filepath)
            for result in data["results"]:
                result["id"] = filename[: -len(".json")]
            all_results.extend(data["results"])
    return all_results


def load_results_jsons(directory):
    all_results = []
    for node_folder in os.listdir(directory):
        node_path = os.path.join(directory, node_folder)
        if not os.path.isdir(node_path):
            continue

        for filename in os.listdir(node_path):
            if filename.endswith(".json"):
                filepath = os.path.join(node_path, filename)
                data = import_benchmark(filepath)
                for result in data["results"]:
                    result["id"] = node_folder
                all_results.extend(data["results"])
    return all_results


def build_1_d_json(data, d_json, base_key=""):
    if isinstance(data, dict):
        for key, value in data.items():
            if isinstance(value, dict):
                build_1_d_json(
                    value, d_json, key if base_key == "" else base_key + "::" + key
                )
            else:
                d_json[key if (base_key == "") else base_key + "::" + key] = []
    return d_json


def transform_into_1_d_json(data, target_json, base_key=""):
    if isinstance(data, dict):
        for key, value in data.items():
            if isinstance(value, dict):
                transform_into_1_d_json(
                    value, target_json, key if base_key == "" else base_key + "::" + key
                )
            else:
                target_json[key if (base_key == "") else base_key + "::" + key] = value
    return target_json


def fill_schema(schema, result, base_key=""):
    for key, value in result.items():
        if isinstance(value, dict):
            fill_schema(schema, value, key if base_key == "" else base_key + "::" + key)
        else:
            schema[key if base_key == "" else base_key + "::" + key].append(value)


def fill_schema_missing_values(schema, result):
    for key, extracted_values in schema.items():
        key_parts = key.split("::")
        scoped_result = result
        for key_part in key_parts:
            if key_part in scoped_result:
                scoped_result = scoped_result[key_part]
            else:
                scoped_result = pd.NA
                break
        extracted_values.append(scoped_result)


def load_flat_benchmark_directory_to_pandas(path):
    results = load_flat_jsons(path)
    schema = build_1_d_json(results[0], {})
    for result in results:
        fill_schema(schema, result)
    return pd.DataFrame(schema)


def load_results_benchmark_directory_to_pandas(path):
    results = load_results_jsons(path)
    schema = build_1_d_json(results[0], {})
    for result in results:
        schema = dict(schema, **build_1_d_json(result, {}))
    for result in results:
        fill_schema_missing_values(schema, result)
    return pd.DataFrame(schema)


def draw_line_left_to_plot(
    fig, ax, offset, text_left, text_right, height=0.9, lower=0.1
):
    line_x = ax.get_position().x0 + offset + 0.004
    fig.add_artist(
        plt.Line2D(
            [line_x, line_x],
            [lower, height],
            color="black",
            linewidth=1,
            linestyle="dashed",
        )
    )
    fig.text(
        line_x - 0.004,
        height / 2 + 0.05,
        text_left,
        fontstyle="italic",
        ha="right",
        va="center",
        fontsize=21,
        rotation=90,
    )
    fig.text(
        line_x + 0.02,
        height / 2 + 0.05,
        text_right,
        fontstyle="italic",
        ha="right",
        va="center",
        fontsize=21,
        rotation=90,
    )


def draw_line_left_to_plot_new(
    fig, ax, offset, text_left, text_right, upper=0.9, lower=0.1
):
    height = upper - lower
    line_x = ax.get_position().x0 + offset + 0.004
    fig.add_artist(
        plt.Line2D(
            [line_x, line_x],
            [lower, upper],
            color="black",
            linewidth=1,
            linestyle="dashed",
        )
    )
    fig.text(
        line_x - 0.004,
        lower + height / 2,
        text_left,
        fontstyle="italic",
        ha="right",
        va="center",
        fontsize=21,
        rotation=90,
    )
    fig.text(
        line_x + 0.02,
        lower + height / 2,
        text_right,
        fontstyle="italic",
        ha="right",
        va="center",
        fontsize=21,
        rotation=90,
    )


def move_axis_right(ax, by):
    ax.set_position(
        [
            ax.get_position().x0 + by,
            ax.get_position().y0,
            ax.get_position().width,
            ax.get_position().height,
        ]
    )


def add_fancy_descriptors(ax, ylabel, xlabel, n_plts, n_col=4, legend_height=1.05):
    ax.set_ylabel(ylabel, fontsize=16)
    ax.legend(
        loc="lower center",
        bbox_to_anchor=(n_plts / 2.0 + 0.5, legend_height),
        ncol=n_col,
        fancybox=True,
        fontsize=15,
    )
    ax.set_xlabel(
        xlabel,
        fontsize=16,
    )
    ax.xaxis.set_label_coords(0.5 + 0.5 * n_plts, -0.02)


if __name__ == "__main__":
    df = load_flat_benchmark_directory_to_pandas(f"{DATA_DIR}/lfb_batched")
    print(df)
