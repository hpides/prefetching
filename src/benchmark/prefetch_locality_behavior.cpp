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
#include "utils/stats.hpp"
#include "utils/simple_continuous_allocator.hpp"

const size_t CACHELINE_SIZE = get_cache_line_size();
const size_t REPEATS = 10;

struct LocalityBenchmarkConfig
{
    size_t total_memory;
    size_t num_threads;
    size_t num_resolves;
    size_t num_resolves_per_measure;
    std::string locality_hint;
    bool bind_prefetch_to_memory_load;
    bool use_explicit_huge_pages;
    bool madvise_huge_pages;
};

template <int locality>
void locality_benchmark(size_t i, size_t number_accesses, const LocalityBenchmarkConfig &config, auto &zero_data, auto &lower_accesses, auto &prefetch_accesses, auto &upper_accesses, auto &prefetch_durations, auto &durations_baseline, auto &durations_upper)
{
    pin_to_cpu(Prefetching::get().numa_manager.node_to_available_cpus[0][i]);
    size_t start_access = i * number_accesses;
    int dummy_dependency = 0; // data has only zeros written to it, so this will effectively do nothing, besides
                              // adding a data dependency - Hopefully this forces batches to be loaded sequentially.

    std::random_device rd;
    std::chrono::duration<double> duration{0};
    std::chrono::duration<double> duration_baseline{0};
    std::chrono::duration<double> duration_upper{0};
    size_t b = 0;
    for (; b + config.num_resolves_per_measure <= config.num_resolves; b += config.num_resolves_per_measure)
    {

        if (config.bind_prefetch_to_memory_load)
        {
            for (int j = 0; j < config.num_resolves_per_measure; ++j)
            {
                __builtin_prefetch(reinterpret_cast<void *>(zero_data.data() + prefetch_accesses[start_access + b + j + dummy_dependency]), 0, locality);
                dummy_dependency += zero_data[lower_accesses[start_access + b + j + dummy_dependency]];
            }
        }
        else
        {
            for (int j = 0; j < config.num_resolves_per_measure; ++j)
            {
                __builtin_prefetch(reinterpret_cast<void *>(zero_data.data() + prefetch_accesses[start_access + b + j]), 0, locality);
            }
            for (int j = 0; j < config.num_resolves_per_measure; ++j)
            {
                dummy_dependency += zero_data[lower_accesses[start_access + b + j + dummy_dependency]];
            }
        }

        // === Baseline Measurement ===
        // Loads Addresses blocking, thus moving them to L1 before actually measuring access duration.

        auto start_baseline = std::chrono::steady_clock::now();
        for (int j = 0; j < config.num_resolves_per_measure; ++j)
        {
            dummy_dependency += zero_data[lower_accesses[start_access + b + j + dummy_dependency]];
        }
        auto end_baseline = std::chrono::steady_clock::now();
        duration_baseline += end_baseline - start_baseline;

        // === Prefetch Measurement ===
        // Prefetch Addresses and then load access latency.

        auto start = std::chrono::steady_clock::now();
        for (int j = 0; j < config.num_resolves_per_measure; ++j)
        {
            dummy_dependency += zero_data[prefetch_accesses[start_access + b + j + dummy_dependency]];
        }
        auto end = std::chrono::steady_clock::now();
        duration += end - start;

        // === Upper Measurement ===
        // Measure latency to random addresses, without any prior prefetches.

        auto start_upper = std::chrono::steady_clock::now();
        for (int j = 0; j < config.num_resolves_per_measure; ++j)
        {
            dummy_dependency += zero_data[upper_accesses[start_access + b + j + dummy_dependency]];
        }
        auto end_upper = std::chrono::steady_clock::now();
        duration_upper += end_upper - start_upper;
    }
    if (dummy_dependency != 0)
    {
        throw std::runtime_error("dummy_dependency wrong: " + std::to_string(dummy_dependency));
    }
    prefetch_durations[i] = duration / b;
    durations_baseline[i] = duration_baseline / b;
    durations_upper[i] = duration_upper / b;
};

template <int locality>
void benchmark_wrapper(LocalityBenchmarkConfig config, nlohmann::json &results, auto &zero_data)
{

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, zero_data.size() - 1);

    std::vector<std::uint64_t> baseline_accesses(config.num_resolves);
    std::vector<std::uint64_t> prefetch_accesses(config.num_resolves);
    std::vector<std::uint64_t> upper_accesses(config.num_resolves);
    std::vector<double> measurement_baseline_durations(REPEATS);
    std::vector<double> measurement_prefetch_durations(REPEATS);
    std::vector<double> measurement_upper_bound_durations(REPEATS);

    for (size_t r = 0; r < REPEATS; r++)
    {
        const size_t number_accesses_per_thread = config.num_resolves / config.num_threads;
        std::vector<std::jthread> threads;

        threads.clear();
        std::generate(baseline_accesses.begin(), baseline_accesses.end(), [&]()
                      { return dis(gen); });
        std::generate(prefetch_accesses.begin(), prefetch_accesses.end(), [&]()
                      { return dis(gen); });
        std::generate(upper_accesses.begin(), upper_accesses.end(), [&]()
                      { return dis(gen); });

        std::vector<std::chrono::duration<double>> baseline_durations(config.num_threads);
        std::vector<std::chrono::duration<double>> prefetch_durations(config.num_threads);
        std::vector<std::chrono::duration<double>> upper_durations(config.num_threads);
        for (size_t i = 0; i < config.num_threads; ++i)
        {
            threads.emplace_back([&, i]()
                                 { locality_benchmark<locality>(i, number_accesses_per_thread, config, zero_data, baseline_accesses, prefetch_accesses, upper_accesses, prefetch_durations, baseline_durations, upper_durations); });
        }
        for (auto &t : threads)
        {
            t.join();
        }
        auto total_prefetch_time = std::chrono::duration<double>{0};
        auto total_baseline_time = std::chrono::duration<double>{0};
        auto total_upper_time = std::chrono::duration<double>{0};
        for (size_t i = 0; i < config.num_threads; ++i)
        {
            total_prefetch_time += prefetch_durations[i];
            total_baseline_time += baseline_durations[i];
            total_upper_time += upper_durations[i];
        }
        measurement_prefetch_durations[r] = total_prefetch_time.count() / config.num_threads;
        measurement_baseline_durations[r] = total_baseline_time.count() / config.num_threads;
        measurement_upper_bound_durations[r] = total_upper_time.count() / config.num_threads;
    }
    generate_stats(results, measurement_prefetch_durations, "");
    generate_stats(results, measurement_baseline_durations, "baseline_");
    generate_stats(results, measurement_upper_bound_durations, "upper_");

    std::cout << "num_resolves_per_measure: " << config.num_resolves_per_measure << " locality: " << config.locality_hint
              << " bind_prefetch_to_memory_load: " << config.bind_prefetch_to_memory_load
              << " madvise_huge_pages: " << config.madvise_huge_pages << std::endl;
    std::cout << "lower runtime: " << results["baseline_runtime"] << " prefetched_runtime: " << results["runtime"] << " upper runtime: " << results["upper_runtime"] << std::endl;
}

int main(int argc, char **argv)
{
    auto &benchmark_config = Prefetching::get().runtime_config;

    // clang-format off
    benchmark_config.add_options()
        ("total_memory", "Total memory allocated MiB", cxxopts::value<std::vector<size_t>>()->default_value("512"))
        ("num_threads", "Number of threads running the bench", cxxopts::value<std::vector<size_t>>()->default_value("1"))
        ("locality_hint", "locality_hint, can be nta, t0, t1, or t2", cxxopts::value<std::vector<std::string>>()->default_value("NTA,T0,T1,T2"))
        ("num_resolves", "Number of total resolves, is set to be at least as large a num_resolves_per_measure", cxxopts::value<std::vector<size_t>>()->default_value("1000000"))
        ("num_resolves_per_measure", "Number of resolves done between start and end of time measurement", cxxopts::value<std::vector<size_t>>()->default_value("32"))
        ("bind_prefetch_to_memory_load", "Executes a load and a prefetch in parallel, limiting the amount of parallel in flight prefetches", cxxopts::value<std::vector<bool>>()->default_value("true,false"))
        ("madvise_huge_pages", "Madvise kernel to create huge pages on mem regions", cxxopts::value<std::vector<bool>>()->default_value("true,false"))
        ("use_explicit_huge_pages", "Use huge pages during allocation", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("out", "Path on which results should be stored", cxxopts::value<std::vector<std::string>>()->default_value("prefetch_locality_behavior.json"));
    // clang-format on
    benchmark_config.parse(argc, argv);

    int benchmark_run = 0;

    std::vector<nlohmann::json> all_results;
    for (auto &runtime_config : benchmark_config.get_runtime_configs())
    {
        auto total_memory = convert<size_t>(runtime_config["total_memory"]);
        auto num_threads = convert<size_t>(runtime_config["num_threads"]);
        auto locality_hint = convert<std::string>(runtime_config["locality_hint"]);
        auto num_resolves = convert<size_t>(runtime_config["num_resolves"]);
        auto num_resolves_per_measure = convert<size_t>(runtime_config["num_resolves_per_measure"]);
        auto bind_prefetch_to_memory_load = convert<bool>(runtime_config["bind_prefetch_to_memory_load"]);
        auto madvise_huge_pages = convert<bool>(runtime_config["madvise_huge_pages"]);
        auto use_explicit_huge_pages = convert<bool>(runtime_config["use_explicit_huge_pages"]);
        auto out = convert<std::string>(runtime_config["out"]);

        num_resolves = std::max(num_resolves, num_resolves_per_measure);
        LocalityBenchmarkConfig config = {
            total_memory,
            num_threads,
            num_resolves,
            num_resolves_per_measure,
            locality_hint,
            bind_prefetch_to_memory_load,
            use_explicit_huge_pages,
            madvise_huge_pages,
        };

        auto total_memory_bytes = config.total_memory * 1024 * 1024; // memory given in MiB
        StaticNumaMemoryResource mem_res{0, config.use_explicit_huge_pages, config.madvise_huge_pages};
        SimpleContinuousAllocator simple_continuous_allocator{mem_res, 1024l * (1 << 20), 512l * (1 << 20)};

        std::pmr::vector<char> data(total_memory_bytes, &simple_continuous_allocator);

        memset(data.data(), total_memory_bytes, 0);
        nlohmann::json results;
        results["config"]["total_memory"] = config.total_memory;
        results["config"]["locality_hint"] = config.locality_hint;
        results["config"]["num_threads"] = config.num_threads;
        results["config"]["num_resolves"] = config.num_resolves;
        results["config"]["num_resolves_per_measure"] = config.num_resolves_per_measure;
        results["config"]["bind_prefetch_to_memory_load"] = config.bind_prefetch_to_memory_load;
        results["config"]["use_explicit_huge_pages"] = config.use_explicit_huge_pages;
        results["config"]["madvise_huge_pages"] = config.madvise_huge_pages;
        if (config.locality_hint == "NTA")
        {
            // _MM_HINT_NTA
            benchmark_wrapper<0>(config, results, data);
        }
        else if (config.locality_hint == "T0")
        {
            // _MM_HINT_T0
            benchmark_wrapper<3>(config, results, data);
        }
        else if (config.locality_hint == "T1")
        {
            // _MM_HINT_T1
            benchmark_wrapper<2>(config, results, data);
        }
        else if (config.locality_hint == "T2")
        {
            // _MM_HINT_T2
            benchmark_wrapper<1>(config, results, data);
        }
        else
        {
            throw std::runtime_error("Unknown locality_hint given: " + config.locality_hint);
        }
        all_results.push_back(results);
        auto results_file = std::ofstream{out};
        nlohmann::json intermediate_json;
        intermediate_json["results"] = all_results;
        results_file << intermediate_json.dump(-1) << std::flush;
    }
    return 0;
}