#include "prefetching.hpp"

#include <iostream>
#include <vector>
#include <random>
#include <fstream>
#include <memory_resource>
#include <chrono>
#include <cstring>

#include <nlohmann/json.hpp>
#include "../../config.hpp"
#include "numa/numa_memory_resource_no_jemalloc.hpp"
#include "../lib/utils/simple_continuous_allocator.hpp"
#include "../lib/utils/utils.hpp"
#include "utils/stats.hpp"

struct MaterializationBenchmarkConfig
{
    size_t num_threads;
    size_t num_lookups;
    size_t size_data_array;
    size_t repeat_lookup_measurement;
    bool run_remote_memory;
    NodeID run_on_node;
    NodeID alloc_on_node;
    bool madvise_huge_pages;
    bool reliability;
    bool profile;
};

const int MAX_PREFETCHING_DISTANCE = 48;
const int STRING_LENGTH = 16; // 16 * sizeof(uint64_t) = 128 Bytes
const uintptr_t reliability_mask = uintptr_t(1) << 60;

template <const size_t cacheline_size>
int benchmark(MaterializationBenchmarkConfig &config, auto &all_results)
{
    std::cout << "place on: " << config.alloc_on_node << ", run on: " << config.run_on_node << std::endl;
    std::cout << "Distance, baseline naive (ns), prefetch naive (ns), baseline memcpy (ns), prefetch memcpy(ns)" << std::endl;
    const size_t num_data_elements = config.size_data_array / sizeof(uint64_t);

    if (Prefetching::get().numa_manager.node_to_available_cpus[config.alloc_on_node].size() > 0)
    {
        pin_to_cpus(Prefetching::get().numa_manager.node_to_available_cpus[config.alloc_on_node]);
    }

    NumaMemoryResourceNoJemalloc mem_res(config.alloc_on_node, false, config.madvise_huge_pages);
    SimpleContinuousAllocator allocator(mem_res, 2048l * (1 << 20), 512l * (1 << 20), get_curr_hostname().starts_with("ca"));

    std::pmr::vector<uint64_t> data_array(num_data_elements, 0, &allocator);

    std::vector<size_t> offsets(config.num_lookups, 0);
    std::vector<uint64_t> collect;
    collect.resize(config.num_lookups * STRING_LENGTH);
    int distance = 0;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> offset_dist(0, num_data_elements - STRING_LENGTH - 1);

    auto measure_lambda = [&](auto f, std::string name)
    {
        auto &perf_manager = Prefetching::get().perf_manager;
        auto mt_event_counter = perf_manager.get_mt_event_counter(1);
        std::vector<double> durations;
        for (int i = 0; i < config.repeat_lookup_measurement; i++)
        {
            for (size_t i = 0; i < config.num_lookups; ++i)
                offsets[i] = offset_dist(gen);
            if (config.profile)
            {
                mt_event_counter.start(0);
            }
            auto start = std::chrono::steady_clock::now();
            f();
            auto end = std::chrono::steady_clock::now();
            if (config.profile)
            {
                mt_event_counter.stop(0);
            }
            durations.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / config.num_lookups);
        }

        nlohmann::json results;
        results["config"]["num_threads"] = config.num_threads;
        results["config"]["num_lookups"] = config.num_lookups;
        results["config"]["size_data_array"] = config.size_data_array;
        results["config"]["run_remote_memory"] = config.run_remote_memory;
        results["config"]["run_on_node"] = config.run_on_node;
        results["config"]["alloc_on_node"] = config.alloc_on_node;
        results["config"]["repeat_lookup_measurement"] = config.repeat_lookup_measurement;
        results["config"]["profile"] = config.profile;
        results["config"]["madvise_huge_pages"] = config.madvise_huge_pages;
        results["config"]["reliability"] = config.reliability;
        results["config"]["prefetch_distance"] = distance;
        results["config"]["variant"] = name;
        if (config.profile)
        {
            perf_manager.result(mt_event_counter, results, config.num_lookups);
        }
        generate_stats(results, durations, "lookup_");
        all_results.push_back(results);
        return results["lookup_runtime"];
    };

    auto const naive_implementation = [&]()
    {
        for (size_t i = 0; i < config.num_lookups; i++)
        {
            auto offset = offsets[i];
            for (unsigned j = 0; j < STRING_LENGTH; j++)
            {
                collect[(i * STRING_LENGTH) + j] = data_array[offset + j];
            }
    } };

    auto const prefetch_implementation = [&]()
    {
        for (size_t i = 0; i < config.num_lookups; i++){
            if(i + distance < config.num_lookups){
                for (unsigned j = 0; j < STRING_LENGTH; j += cacheline_size / sizeof(uint64_t))
                {
                    __builtin_prefetch(std::addressof(data_array[offsets[i + distance] + j]), 0, 0); // not perfect as offsets are not aligned
                }
            }
            auto offset = offsets[i];
            for (unsigned j = 0; j < STRING_LENGTH; j++)
            {
                collect[(i * STRING_LENGTH) + j] = data_array[offset + j];
            }
    } };

    auto const prefetch_implementation_reliability = [&]()
    {
        for (size_t i = 0; i < config.num_lookups; i++){
            if(i + distance < config.num_lookups){
                for (unsigned j = 0; j < STRING_LENGTH; j += cacheline_size / sizeof(uint64_t))
                {
                    auto address = std::addressof(data_array[offsets[i + distance] + j]);
                    address = reinterpret_cast<uint64_t *>(reinterpret_cast<uintptr_t>(address) | reliability_mask);

                    __builtin_prefetch(address, 0, 0); // not perfect as offsets are not aligned
                }
            }
            auto offset = offsets[i];
            for (unsigned j = 0; j < STRING_LENGTH; j++)
            {
                collect[(i * STRING_LENGTH) + j] = data_array[offset + j];
            }
    } };

    auto const memcpy = [&]()
    {
        for (size_t i = 0; i < config.num_lookups; i++){
            auto offset = offsets[i];
            std::memcpy(reinterpret_cast<std::byte*>(collect.data()) + (STRING_LENGTH * sizeof(uint64_t) * i), std::addressof(data_array[offset]), STRING_LENGTH * sizeof(uint64_t));
    } };

    auto const prefetch_memcpy = [&]()
    {
        for (size_t i = 0; i < config.num_lookups; i++)
        {
            if (i + distance < config.num_lookups)
            {
                for (unsigned j = 0; j < STRING_LENGTH; j += cacheline_size / sizeof(uint64_t))
                {
                    __builtin_prefetch(std::addressof(data_array[offsets[i + distance] + j]), 0, 0); // not perfect as offsets are not aligned
                }
            }
            auto offset = offsets[i];
            std::memcpy(reinterpret_cast<std::byte *>(collect.data()) + (STRING_LENGTH * sizeof(uint64_t) * i), std::addressof(data_array[offset]), STRING_LENGTH * sizeof(uint64_t));
        }
    };

    auto const prefetch_memcpy_reliability = [&]()
    {
        for (size_t i = 0; i < config.num_lookups; i++)
        {
            if (i + distance < config.num_lookups)
            {
                for (unsigned j = 0; j < STRING_LENGTH; j += cacheline_size / sizeof(uint64_t))
                {
                    auto address = std::addressof(data_array[offsets[i + distance] + j]);
                    address = reinterpret_cast<uint64_t *>(reinterpret_cast<uintptr_t>(address) | reliability_mask);

                    __builtin_prefetch(address, 0, 0); // not perfect as offsets are not aligned
                }
            }
            auto offset = offsets[i];
            std::memcpy(reinterpret_cast<std::byte *>(collect.data()) + (STRING_LENGTH * sizeof(uint64_t) * i), std::addressof(data_array[offset]), STRING_LENGTH * sizeof(uint64_t));
        }
    };

    if (Prefetching::get().numa_manager.node_to_available_cpus[config.run_on_node].size() > 0)
    {
        pin_to_cpus(Prefetching::get().numa_manager.node_to_available_cpus[config.run_on_node]);
    }

    auto baseline_runtime = measure_lambda(naive_implementation, "baseline_naive");
    auto memcpy_runtime = measure_lambda(memcpy, "baseline_memcpy");

    for (; distance < MAX_PREFETCHING_DISTANCE; ++distance)
    {
        auto prefetch_runtime = measure_lambda((config.reliability) ? std::function<void()>(prefetch_implementation_reliability) : std::function<void()>(prefetch_implementation), "prefetch_naive");
        auto memcpy_prefetch_runtime = measure_lambda((config.reliability) ? std::function<void()>(prefetch_implementation_reliability) : std::function<void()>(prefetch_memcpy), "prefetch_memcpy");
        std::cout << distance << ",  " << baseline_runtime << ", " << prefetch_runtime << ", " << memcpy_runtime << ", " << memcpy_prefetch_runtime << std::endl;
    }
    return 0;
}
void run_benchmark_cacheline_size(MaterializationBenchmarkConfig &config, std::vector<nlohmann::json> &results)
{
    const size_t cache_line_size = get_cache_line_size();
    if (cache_line_size == 64)
    {
        benchmark<64>(config, results);
    }
    else if (cache_line_size == 256)
    {
        benchmark<256>(config, results);
    }
    else
    {
        throw std::runtime_error("Invalid cache-line-size encountered: " + std::to_string(cache_line_size));
    }
}

int main(int argc, char **argv)
{
    auto &benchmark_config = Prefetching::get().runtime_config;
    auto &perf_manager = Prefetching::get().perf_manager;
    perf_manager.initialize_counter_definition(get_default_perf_config_file());

    // clang-format off
    benchmark_config.add_options()
        ("num_threads", "Number of num_threads", cxxopts::value<std::vector<size_t>>()->default_value("1"))
        ("num_lookups", "Number of lookups", cxxopts::value<std::vector<size_t>>()->default_value("5000000"))
        ("size_data_array", "Size of the data array from which data is copied, in Bytes", cxxopts::value<std::vector<size_t>>()->default_value("536870912")) // 512 MiB
        ("repeat_lookup_measurement", "Number of times the lookup benchmark shall be repeated", cxxopts::value<std::vector<size_t>>()->default_value("5"))
        ("run_remote_memory", "Attempts to load remote memory NumaSetting from config.", cxxopts::value<std::vector<bool>>()->default_value("true,false"))
        ("run_on_node", "Which NUMA node to run the benchmark on.", cxxopts::value<std::vector<NodeID>>()->default_value("0"))
        ("alloc_on_node", "Which NUMA node to alloc memory on.", cxxopts::value<std::vector<NodeID>>()->default_value("0"))
        ("madvise_huge_pages", "Madvise kernel to create huge pages on mem regions", cxxopts::value<std::vector<bool>>()->default_value("true,false"))
        ("reliability", "Fujitsu feature true -> weak reliability, else strong", cxxopts::value<std::vector<bool>>()->default_value("false,true"))
        ("profile", "Profile", cxxopts::value<std::vector<bool>>()->default_value("true,false"))
        ("out", "filename", cxxopts::value<std::vector<std::string>>()->default_value("materialization.json"));
    // clang-format on
    benchmark_config.parse(argc, argv);

    int benchmark_run = 0;
    std::vector<nlohmann::json> all_results;
    for (auto &runtime_config : benchmark_config.get_runtime_configs())
    {
        auto num_threads = convert<size_t>(runtime_config["num_threads"]);
        auto num_lookups = convert<size_t>(runtime_config["num_lookups"]);
        auto size_data_array = convert<size_t>(runtime_config["size_data_array"]);
        auto repeat_lookup_measurement = convert<size_t>(runtime_config["repeat_lookup_measurement"]);
        auto run_remote_memory = convert<bool>(runtime_config["run_remote_memory"]);
        auto run_on_node = convert<NodeID>(runtime_config["run_on_node"]);
        auto alloc_on_node = convert<NodeID>(runtime_config["alloc_on_node"]);
        auto madvise_huge_pages = convert<bool>(runtime_config["madvise_huge_pages"]);
        auto reliability = convert<bool>(runtime_config["reliability"]);
        auto profile = convert<bool>(runtime_config["profile"]);

        if (reliability && !get_curr_hostname().starts_with("ca"))
        {
            // Ignore reliability = True on non Fujitsu nodes
            continue;
        }
        if (run_remote_memory)
        {
            // TODO: Fallback to some default incase no entry is given for hostname, perhaps?
            auto remote_numa_config = get_config_entry(get_curr_hostname(), HOST_TO_REMOTE_NUMA_CONFIG);
            run_on_node = remote_numa_config.run_on;
            alloc_on_node = remote_numa_config.alloc_on;
        }

        auto numa_config = NumaConfig{run_on_node, alloc_on_node};
        if (numa_config.run_on == NodeID{0} && numa_config.alloc_on == NodeID{0}) // if default params
        {
            if (run_remote_memory)
            {
                numa_config = get_config_entry(get_curr_hostname(), HOST_TO_REMOTE_NUMA_CONFIG);
            }
            else
            {
                numa_config = get_config_entry_default(get_curr_hostname(), HOST_TO_LOCAL_NUMA_CONFIG, {0, 0});
            }
        }
        run_on_node = numa_config.run_on;
        alloc_on_node = numa_config.alloc_on;
        if (profile)
        {
            repeat_lookup_measurement = 1;
        }

        MaterializationBenchmarkConfig config =
            {
                num_threads,
                num_lookups,
                size_data_array,
                repeat_lookup_measurement,
                run_remote_memory,
                run_on_node,
                alloc_on_node,
                madvise_huge_pages,
                reliability,
                profile,
            };

        run_benchmark_cacheline_size(config, all_results);

        auto results_file = std::ofstream{convert<std::string>(runtime_config["out"])};
        nlohmann::json intermediate_json;
        intermediate_json["results"] = all_results;
        results_file << intermediate_json.dump(-1) << std::flush;
    }

    return 0;
}
