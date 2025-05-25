#include "prefetching.hpp"

#include <random>
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>

#include <nlohmann/json.hpp>

#include "numa/numa_memory_resource.hpp"
#include "numa/static_numa_memory_resource.hpp"
#include "utils/utils.cpp"

const size_t CACHELINE_SIZE = get_cache_line_size();

struct LFBBenchmarkConfig
{
    size_t total_memory;
    size_t num_threads;
    size_t num_total_accesses;
    size_t batch_size;
    bool use_explicit_huge_pages;
    bool madvise_huge_pages;
};

void batched_load(size_t i, size_t number_accesses, auto &config, auto &data, auto &accesses, auto &durations)
{
    pin_to_cpu(Prefetching::get().numa_manager.node_to_available_cpus[0][i]);
    size_t start_access = i * number_accesses;
    int dummy_dependency = 0; // data has only zeros written to it, so this will effectively do nothing, besides
                              // adding a data dependency - Hopefully this forces batches to be loaded sequentially.

    const auto num_batches = number_accesses / config.batch_size;
    const auto data_size = data.size();
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t b = 0; b < num_batches; ++b)
    {
        auto offset = start_access + (b * config.batch_size); // thread offset
        for (size_t i = 0; i < config.batch_size; ++i)
        {
            auto random_access = accesses[offset + config.prefetch_distance * config.batch_size + i] + dummy_dependency;
            for (size_t j = 0; j < config.resolve_cachelines; j++)
            {
                __builtin_prefetch(reinterpret_cast<void *>(data.data() + random_access + CACHELINE_SIZE * j), 0, 0);
            }
        }
        for (size_t i = 0; i < config.batch_size; ++i)
        {
            auto random_access = accesses[offset + i] + dummy_dependency;
            for (size_t j = 0; j < config.resolve_cachelines; j++)
            {
                dummy_dependency += *reinterpret_cast<uint8_t *>(data.data() + random_access + CACHELINE_SIZE * j);
            }
        }
        // LFB CLEAR
        // idle computation
        // noops
    }
    if (dummy_dependency > data_size)
    {
        throw std::runtime_error("new_dep contains wrong dependency: " + std::to_string(dummy_dependency));
    }
    auto end = std::chrono::high_resolution_clock::now();
    durations[i] = std::chrono::duration<double>(end - start);
};

void batched_load_simplified(size_t i, size_t number_accesses, auto &config, auto &data, auto &accesses, auto &durations)
{
    pin_to_cpu(Prefetching::get().numa_manager.node_to_available_cpus[0][i]);
    size_t start_access = i * number_accesses;
    int dummy_dependency = 0; // data has only zeros written to it, so this will effectively do nothing, besides
                              // adding a data dependency - Hopefully this forces batches to be loaded sequentially.

    const auto data_size = data.size();
    auto start = std::chrono::high_resolution_clock::now();
    const size_t iterations = number_accesses / config.batch_size;
    for (size_t b = 0; b < iterations; ++b)
    {
        auto offset = start_access + b * config.batch_size; // thread offset
        const auto batch_dependency = dummy_dependency;
        for (size_t j = 0; j < config.batch_size; j++)
        {
            const auto random_access = accesses[offset + j] + batch_dependency;
            dummy_dependency += *reinterpret_cast<uint8_t *>(data.data() + random_access);
        }
    }
    if (dummy_dependency > data_size)
    {
        throw std::runtime_error("new_dep contains wrong dependency: " + std::to_string(dummy_dependency));
    }
    auto end = std::chrono::high_resolution_clock::now();
    durations[i] = std::chrono::duration<double>(end - start);
};

void lfb_size_benchmark(LFBBenchmarkConfig config, nlohmann::json &results, auto &zero_data)
{

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, zero_data.size() - 1);

    std::vector<std::uint64_t> accesses(config.num_total_accesses);
    std::vector<std::uint64_t> zero_accesses(config.num_total_accesses, 0);

    const size_t read_size = 8;
    // fill accesses with random numbers from 0 to total_memory (in bytes) - read_size.
    std::generate(accesses.begin(), accesses.end(), [&]()
                  { return dis(gen); });

    auto min_time = std::chrono::duration<double>{std::numeric_limits<double>::max()}.count();

    for (int i = 0; i < 5; ++i)
    {
        std::vector<std::jthread> baselines_threads;

        std::vector<std::chrono::duration<double>> baseline_durations(config.num_threads);
        size_t number_accesses_per_thread = config.num_total_accesses / config.num_threads;
        for (size_t i = 0; i < config.num_threads; ++i)
        {
            baselines_threads.emplace_back([&, i]()
                                           { batched_load_simplified(i, number_accesses_per_thread, config, zero_data, zero_accesses, baseline_durations); });
        }
        for (auto &t : baselines_threads)
        {
            t.join();
        }
        auto baseline_total_time = std::chrono::duration<double>{0};
        for (auto &duration : baseline_durations)
        {
            baseline_total_time += duration;
        }

        std::vector<std::jthread> threads;

        std::vector<std::chrono::duration<double>> durations(config.num_threads);
        for (size_t i = 0; i < config.num_threads; ++i)
        {
            threads.emplace_back([&, i]()
                                 { batched_load_simplified(i, number_accesses_per_thread, config, zero_data, accesses, durations); });
        }
        for (auto &t : threads)
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
    std::cout << "batch_size: " << config.batch_size << std::endl;
    std::cout << "took " << min_time << std::endl;
}

int main(int argc, char **argv)
{
    auto &benchmark_config = Prefetching::get().runtime_config;

    // clang-format off
    benchmark_config.add_options()
        ("total_memory", "Total memory allocated MiB", cxxopts::value<std::vector<size_t>>()->default_value("1024"))
        ("num_threads", "Number of threads running the bench", cxxopts::value<std::vector<size_t>>()->default_value("8"))
        ("num_total_accesses", "Number of repetitions of the measurement", cxxopts::value<std::vector<size_t>>()->default_value("10000000"))
        ("start_batch_size", "starting number of batch size", cxxopts::value<std::vector<size_t>>()->default_value("1"))
        ("end_batch_size", "ending number of batch size", cxxopts::value<std::vector<size_t>>()->default_value("64"))
        ("madvise_huge_pages", "Madvise kernel to create huge pages on mem regions", cxxopts::value<std::vector<bool>>()->default_value("true"))
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
        auto num_total_accesses = convert<size_t>(runtime_config["num_total_accesses"]);
        auto start_batch_size = convert<size_t>(runtime_config["start_batch_size"]);
        auto end_batch_size = convert<size_t>(runtime_config["end_batch_size"]);
        auto madvise_huge_pages = convert<bool>(runtime_config["madvise_huge_pages"]);
        auto use_explicit_huge_pages = convert<bool>(runtime_config["use_explicit_huge_pages"]);
        auto out = convert<std::string>(runtime_config["out"]);

        LFBBenchmarkConfig config = {
            total_memory,
            num_threads,
            num_total_accesses,
            start_batch_size,
            use_explicit_huge_pages,
            madvise_huge_pages,
        };

        auto total_memory_bytes = config.total_memory * 1024 * 1024; // memory given in MiB
        StaticNumaMemoryResource mem_res{0, config.use_explicit_huge_pages, config.madvise_huge_pages};

        std::pmr::vector<char> data(total_memory_bytes, &mem_res);

        memset(data.data(), total_memory_bytes, 0);
        // sleep(total_memory / 1.2);
        for (size_t batch_size = start_batch_size; batch_size <= end_batch_size; batch_size++)
        {
            nlohmann::json results;
            results["config"]["total_memory"] = config.total_memory;
            results["config"]["num_threads"] = config.num_threads;
            results["config"]["num_total_accesses"] = config.num_total_accesses;
            results["config"]["batch_size"] = config.batch_size;
            results["config"]["use_explicit_huge_pages"] = config.use_explicit_huge_pages;
            results["config"]["madvise_huge_pages"] = config.madvise_huge_pages;
            config.batch_size = batch_size;
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