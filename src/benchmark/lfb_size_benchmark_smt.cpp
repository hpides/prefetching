#include "prefetching.hpp"

#include <random>
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>
#include <atomic>
#include <algorithm>

#include <nlohmann/json.hpp>

#include "numa/numa_memory_resource.hpp"
#include "numa/static_numa_memory_resource.hpp"
#include "utils/utils.cpp"

const size_t CACHELINE_SIZE = get_cache_line_size();

struct LFBBenchmarkConfig
{
    size_t total_memory;
    size_t num_threads;
    size_t num_repetitions;
    size_t parallel_load;
    bool load_prefetch;
    bool use_explicit_huge_pages;
    bool madvise_huge_pages;
};

void load_thread(std::atomic<bool> &keep_running, size_t thread_offset, NodeID cpu_id, size_t number_accesses, auto &config, auto &data, auto &accesses)
{
    pin_to_cpu(cpu_id);
    size_t start_access = thread_offset * number_accesses;
    int dummy_dependency = 0; // data has only zeros written to it, so this will effectively do nothing, besides
                              // adding a data dependency - Hopefully this forces batches to be loaded sequentially.

    const auto num_batches = number_accesses / config.parallel_load;
    const auto data_size = data.size();
    while (keep_running.load())
    {
        for (size_t b = 0; b < num_batches; ++b)
        {
            auto index = b * config.parallel_load + start_access + dummy_dependency;
            if (config.load_prefetch)
            {
                for (size_t i = 0; i < config.parallel_load; ++i)
                {
                    __builtin_prefetch(data.data() + accesses[index + i], 0, 0);
                }
            }
            for (size_t i = 0; i < config.parallel_load; ++i)
            {
                dummy_dependency += *reinterpret_cast<uint8_t *>(data.data() + accesses[index + i]);
            }
        }

        if (dummy_dependency > data_size)
        {
            throw std::runtime_error("new_dep contains wrong dependency: " + std::to_string(dummy_dependency));
        }
    }
};

void measure_thread(size_t thread_offset, NodeID cpu_id, size_t number_accesses, auto &config, auto &data, auto &accesses, auto &durations)
{
    pin_to_cpu(cpu_id);
    size_t start_access = thread_offset * number_accesses;
    int dummy_dependency = 0; // data has only zeros written to it, so this will effectively do nothing, besides
                              // adding a data dependency - Hopefully this forces batches to be loaded sequentially.

    const auto data_size = data.size();
    auto start = std::chrono::high_resolution_clock::now();
    const size_t BATCH_SIZE = 1;
    const size_t iterations = number_accesses / BATCH_SIZE;
    for (size_t b = 0; b < iterations; ++b)
    {
        auto batch_dependency = start_access + b * BATCH_SIZE + dummy_dependency;
        for (size_t i = 0; i < 5; ++i)
        {
            dummy_dependency += *reinterpret_cast<uint8_t *>(data.data() + accesses[batch_dependency + i]);
        }
    }
    if (dummy_dependency > data_size)
    {
        throw std::runtime_error("new_dep contains wrong dependency: " + std::to_string(dummy_dependency));
    }
    auto end = std::chrono::high_resolution_clock::now();

    durations[thread_offset] = std::chrono::duration<double>(end - start);
};

void lfb_size_benchmark(LFBBenchmarkConfig config, nlohmann::json &results, auto &zero_data)
{
    const auto &numa_manager = Prefetching::get().numa_manager;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, zero_data.size() - 1);

    std::vector<std::uint64_t> accesses(config.num_repetitions);
    std::vector<std::uint64_t> load_accesses(config.num_repetitions);
    std::vector<std::uint64_t> zero_accesses(config.num_repetitions, 0);

    const size_t read_size = 8;
    // fill accesses with random numbers from 0 to total_memory (in bytes) - read_size.
    std::generate(accesses.begin(), accesses.end(), [&]()
                  { return dis(gen); });

    // fill load_accesses with the reversed content of accesses
    std::reverse_copy(accesses.begin(), accesses.end(), load_accesses.begin());

    auto min_time = std::chrono::duration<double>{std::numeric_limits<double>::max()}.count();

    for (int i = 0; i < 5; ++i)
    {
        std::vector<std::jthread> baselines_threads_load;
        std::vector<std::jthread> baselines_threads_measure;

        std::vector<std::chrono::duration<double>> baseline_durations(config.num_threads);
        size_t number_accesses_per_thread = config.num_repetitions / config.num_threads;
        std::atomic<bool> keep_running = true;
        for (size_t i = 0; i < config.num_threads; ++i)
        {
            baselines_threads_load.emplace_back([&, i]()
                                                { load_thread(keep_running, i, numa_manager.socket_and_core_id_to_cpu[0][i][1], number_accesses_per_thread, config, zero_data, zero_accesses); });
        }

        for (size_t i = 0; i < config.num_threads; ++i)
        {
            baselines_threads_measure.emplace_back([&, i]()
                                                   { measure_thread(i, numa_manager.socket_and_core_id_to_cpu[0][i][0], number_accesses_per_thread, config, zero_data, zero_accesses, baseline_durations); });
        }
        for (auto &t : baselines_threads_measure)
        {
            t.join();
        }
        keep_running = false;
        for (auto &t : baselines_threads_load)
        {
            t.join();
        }

        auto baseline_total_time = std::chrono::duration<double>{0};
        for (auto &duration : baseline_durations)
        {
            baseline_total_time += duration;
        }

        std::vector<std::jthread> measure_threads;
        std::vector<std::jthread> load_threads;

        std::vector<std::chrono::duration<double>> durations(config.num_threads);
        keep_running = true;
        for (size_t i = 0; i < config.num_threads; ++i)
        {
            load_threads.emplace_back([&, i]()
                                      { load_thread(keep_running, i, numa_manager.socket_and_core_id_to_cpu[0][i][1], number_accesses_per_thread, config, zero_data, load_accesses); });
        }

        for (size_t i = 0; i < config.num_threads; ++i)
        {
            measure_threads.emplace_back([&, i]()
                                         { measure_thread(i, numa_manager.socket_and_core_id_to_cpu[0][i][0], number_accesses_per_thread, config, zero_data, accesses, durations); });
        }
        for (auto &t : measure_threads)
        {
            t.join();
        }
        keep_running = false;
        for (auto &t : load_threads)
        {
            t.join();
        }
        auto total_time = std::chrono::duration<double>{0};
        for (auto &duration : durations)
        {
            total_time += duration;
        }
        min_time = std::min(min_time, (total_time - baseline_total_time).count());
    }

    results["runtime"] = min_time;
    std::cout << "parallel_load: " << config.parallel_load << std::endl;
    std::cout << "took " << min_time << std::endl;
}

int main(int argc, char **argv)
{
    auto &benchmark_config = Prefetching::get().runtime_config;

    // clang-format off
    benchmark_config.add_options()
        ("total_memory", "Total memory allocated MiB", cxxopts::value<std::vector<size_t>>()->default_value("1024"))
        ("num_threads", "Number of threads running the bench", cxxopts::value<std::vector<size_t>>()->default_value("1"))
        ("num_repetitions", "Number of repetitions of the measurement", cxxopts::value<std::vector<size_t>>()->default_value("10000000"))
        ("start_parallel_load", "starting number of parallel_load on load thread", cxxopts::value<std::vector<size_t>>()->default_value("1"))
        ("end_parallel_load", "ending number of parallel_load on load thread", cxxopts::value<std::vector<size_t>>()->default_value("64"))
        ("madvise_huge_pages", "Madvise kernel to create huge pages on mem regions", cxxopts::value<std::vector<bool>>()->default_value("true"))
        ("load_prefetch", "Prefetch whole batch on load thread before accessing", cxxopts::value<std::vector<bool>>()->default_value("true,false"))
        ("use_explicit_huge_pages", "Use huge pages during allocation", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("out", "Path on which results should be stored", cxxopts::value<std::vector<std::string>>()->default_value("lfb_size.json"));
    // clang-format on
    benchmark_config.parse(argc, argv);

    int benchmark_run = 0;

    std::vector<nlohmann::json> all_results;
    for (auto &runtime_config : benchmark_config.get_runtime_configs())
    {
        auto total_memory = convert<size_t>(runtime_config["total_memory"]);
        auto num_threads = convert<size_t>(runtime_config["num_threads"]);
        auto num_repetitions = convert<size_t>(runtime_config["num_repetitions"]);
        auto start_parallel_load = convert<size_t>(runtime_config["start_parallel_load"]);
        auto end_parallel_load = convert<size_t>(runtime_config["end_parallel_load"]);
        auto load_prefetch = convert<bool>(runtime_config["load_prefetch"]);
        auto madvise_huge_pages = convert<bool>(runtime_config["madvise_huge_pages"]);
        auto use_explicit_huge_pages = convert<bool>(runtime_config["use_explicit_huge_pages"]);
        auto out = convert<std::string>(runtime_config["out"]);

        LFBBenchmarkConfig config = {
            total_memory,
            num_threads,
            num_repetitions,
            start_parallel_load,
            load_prefetch,
            use_explicit_huge_pages,
            madvise_huge_pages,
        };

        auto total_memory_bytes = config.total_memory * 1024 * 1024; // memory given in MiB
        StaticNumaMemoryResource mem_res{0, config.use_explicit_huge_pages, config.madvise_huge_pages};

        std::pmr::vector<char> data(total_memory_bytes, &mem_res);

        memset(data.data(), total_memory_bytes, 0);
        // sleep(total_memory / 1.2);
        for (size_t parallel_load = start_parallel_load; parallel_load <= end_parallel_load; parallel_load++)
        {
            nlohmann::json results;
            results["config"]["total_memory"] = config.total_memory;
            results["config"]["num_threads"] = config.num_threads;
            results["config"]["num_repetitions"] = config.num_repetitions;
            results["config"]["parallel_load"] = config.parallel_load;
            results["config"]["load_prefetch"] = config.load_prefetch;
            results["config"]["use_explicit_huge_pages"] = config.use_explicit_huge_pages;
            results["config"]["madvise_huge_pages"] = config.madvise_huge_pages;
            config.parallel_load = parallel_load;
            lfb_size_benchmark(config, results, data);
            all_results.push_back(results);
        }
        auto results_file = std::ofstream{out};
        nlohmann::json intermediate_json;
        intermediate_json["results"] = all_results;
        results_file << intermediate_json.dump(-1) << std::flush;
    }

    return 0;
}