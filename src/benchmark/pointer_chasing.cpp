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
const size_t ACTUAL_PAGE_SIZE = get_page_size();
const size_t REPEAT_MEASUREMENT = 10;
struct PCBenchmarkConfig
{
    size_t total_memory;
    size_t num_threads;
    size_t num_resolves;
    size_t num_parallel_pc;
    bool prefetch;
    bool use_explicit_huge_pages;
    bool madvise_huge_pages;
};

void pointer_chase(size_t thread_id, const PCBenchmarkConfig &config, auto &data, auto &durations)
{
    const auto &numa_manager = Prefetching::get().numa_manager;
    pin_to_cpu(numa_manager.node_to_available_cpus[numa_manager.active_nodes[0]][thread_id]);
    uint8_t dependency = 0;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> uniform_dis(0, data.size() - 1);

    std::vector<uint64_t> curr_pointers;
    for (int i = 0; i < config.num_parallel_pc; ++i)
    {
        curr_pointers.push_back(uniform_dis(gen));
    }

    const auto number_repetitions = config.num_resolves / config.num_parallel_pc;

    auto start = std::chrono::steady_clock::now();
    uint32_t old_work_sum = 0;
    for (size_t r = 0; r < number_repetitions; r++)
    {
        if (config.prefetch)
        {
            for (auto &random_pointer : curr_pointers)
            {
                __builtin_prefetch(reinterpret_cast<void *>(data.data() + random_pointer + old_work_sum), 0, 0);
            }
        }
        uint32_t work_sum = 0;
        for (auto &random_pointer : curr_pointers)
        {
            work_sum += reinterpret_cast<size_t>(data.data() + random_pointer + old_work_sum) & 1;
            random_pointer = *(data.data() + random_pointer + old_work_sum);
        }
        old_work_sum = work_sum;
    }
    auto end = std::chrono::steady_clock::now();

    durations[thread_id] = end - start;
};

void lfb_size_benchmark(PCBenchmarkConfig config, nlohmann::json &results, auto &pointer_chase_arr)
{
    StaticNumaMemoryResource mem_res{Prefetching::get().numa_manager.active_nodes[0], config.use_explicit_huge_pages, config.madvise_huge_pages};
    std::random_device rd;
    std::mt19937 gen(rd());

    std::vector<double> final_measurements(REPEAT_MEASUREMENT);
    for (size_t repeat = 0; repeat < REPEAT_MEASUREMENT; repeat++)
    {
        std::vector<std::jthread> threads;
        // ---- Baseline ----
        auto warm_up_config = PCBenchmarkConfig{config};
        std::vector<std::chrono::duration<double>> baseline_durations(config.num_threads);
        std::vector<size_t> pc_single = {0};
        for (size_t i = 0; i < config.num_threads; ++i)
        {
            threads.emplace_back([&, i]()
                                 { pointer_chase(i, config, pc_single, baseline_durations); });
        }
        for (auto &t : threads)
        {
            t.join();
        }
        threads.clear();
        // ---- End Warm-up ----
        std::vector<std::chrono::duration<double>> durations(config.num_threads);
        for (size_t i = 0; i < config.num_threads; ++i)
        {
            threads.emplace_back([&, i]()
                                 { pointer_chase(i, config, pointer_chase_arr, durations); });
        }
        for (auto &t : threads)
        {
            t.join();
        }
        auto total_time = std::chrono::duration<double>{0};
        for (size_t i = 0; i < durations.size(); i++)
        {
            total_time += durations[i] - baseline_durations[i];
        }
        final_measurements[repeat] = total_time.count();
    }

    results["min_runtime"] = *std::min_element(final_measurements.begin(), final_measurements.end());
    results["median_runtime"] = findMedian(final_measurements, final_measurements.size());
    results["runtime"] = results["median_runtime"];
    results["runtimes"] = final_measurements;
    std::cout << config.num_parallel_pc << " took " << results["median_runtime"] << std::endl;
}

int main(int argc, char **argv)
{
    auto &benchmark_config = Prefetching::get().runtime_config;

    // clang-format off
    benchmark_config.add_options()
        ("total_memory", "Total memory allocated MiB", cxxopts::value<std::vector<size_t>>()->default_value("1024"))
        ("num_threads", "Number of threads running the bench", cxxopts::value<std::vector<size_t>>()->default_value("1"))
        ("num_resolves", "Number of resolves each pointer chase executes", cxxopts::value<std::vector<size_t>>()->default_value("1000000"))
        ("start_num_parallel_pc", "Start number of parallel pointer chases per thread", cxxopts::value<std::vector<size_t>>()->default_value("1"))
        ("end_num_parallel_pc", "End number of parallel pointer chases per thread", cxxopts::value<std::vector<size_t>>()->default_value("128"))
        ("use_explicit_huge_pages", "Use huge pages during allocation", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("prefetch", "Madvise kernel to create huge pages on mem regions", cxxopts::value<std::vector<bool>>()->default_value("false,true"))
        ("madvise_huge_pages", "Madvise kernel to create huge pages on mem regions", cxxopts::value<std::vector<bool>>()->default_value("true"))
        ("out", "Path on which results should be stored", cxxopts::value<std::vector<std::string>>()->default_value("pc_benchmark.json"));
    // clang-format on
    benchmark_config.parse(argc, argv);

    int benchmark_run = 0;

    std::vector<nlohmann::json> all_results;
    for (auto &runtime_config : benchmark_config.get_runtime_configs())
    {
        auto total_memory = convert<size_t>(runtime_config["total_memory"]);
        auto num_threads = convert<size_t>(runtime_config["num_threads"]);
        auto num_resolves = convert<size_t>(runtime_config["num_resolves"]);
        auto start_num_parallel_pc = convert<size_t>(runtime_config["start_num_parallel_pc"]);
        auto end_num_parallel_pc = convert<size_t>(runtime_config["end_num_parallel_pc"]);
        auto use_explicit_huge_pages = convert<bool>(runtime_config["use_explicit_huge_pages"]);
        auto madvise_huge_pages = convert<bool>(runtime_config["madvise_huge_pages"]);
        auto prefetch = convert<bool>(runtime_config["prefetch"]);
        auto out = convert<std::string>(runtime_config["out"]);

        PCBenchmarkConfig config = {
            total_memory,
            num_threads,
            num_resolves,
            start_num_parallel_pc,
            prefetch,
            use_explicit_huge_pages,
            madvise_huge_pages,
        };

        StaticNumaMemoryResource mem_res{Prefetching::get().numa_manager.active_nodes[0], config.use_explicit_huge_pages, config.madvise_huge_pages};

        auto num_bytes = config.total_memory * 1024 * 1024; // memory given in MiB

        std::pmr::vector<uint64_t> pc_array(num_bytes / sizeof(uint64_t), &mem_res);
        initialize_pointer_chase(pc_array.data(), pc_array.size());

        for (size_t num_parallel_pc = start_num_parallel_pc; num_parallel_pc <= end_num_parallel_pc; num_parallel_pc++)
        {
            nlohmann::json results;
            results["config"]["total_memory"] = config.total_memory;
            results["config"]["num_threads"] = config.num_threads;
            results["config"]["num_resolves"] = config.num_resolves;
            results["config"]["num_parallel_pc"] = config.num_parallel_pc;
            results["config"]["use_explicit_huge_pages"] = config.use_explicit_huge_pages;
            results["config"]["madvise_huge_pages"] = config.madvise_huge_pages;
            results["config"]["prefetch"] = config.prefetch;

            config.num_parallel_pc = num_parallel_pc;
            lfb_size_benchmark(config, results, pc_array);
            all_results.push_back(results);
            auto results_file = std::ofstream{out};
            nlohmann::json intermediate_json;
            intermediate_json["results"] = all_results;
            results_file << intermediate_json.dump(-1) << std::flush;
        }
    }

    return 0;
}
