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

size_t CACHELINE_SIZE = get_cache_line_size();
size_t ACTUAL_PAGE_SIZE = get_page_size();
const auto CLOCK_MIN_DURATION = get_steady_clock_min_duration(1'000'000);

/*
   We also tried to copy the Dortmund papers approach by randomly fetching blocks of differently sized Cache Line blocks.
   By measuring the average prefetch latency, we attempted to approximate the size of the LFB.
   This is stupid as from a certain block size on, the Hardware prefetchers pick up as the memory access is sequential for
   every cache line block. Code can be seen below:
*/

struct PBCBenchmarkConfig
{
    size_t total_memory;
    size_t num_threads;
    size_t num_resolves;
    size_t num_cache_lines;
    bool use_explicit_huge_pages;
    bool madvise_huge_pages;
};

size_t COMPUTE_FACTOR = 32U;
void pointer_block_chase(size_t thread_id, PBCBenchmarkConfig &config, auto &data, auto &durations)
{
    pin_to_cpu(Prefetching::get().numa_manager.node_to_available_cpus[0][thread_id]);
    uint8_t dependency = 0;
    std::random_device rd;
    std::mt19937 gen(rd());
    size_t data_size_bytes = get_data_size_in_bytes(data);

    std::uniform_int_distribution<> uniform_dis(0, data_size_bytes - 1 - CACHELINE_SIZE * config.num_cache_lines);

    std::vector<uint64_t> curr_pointers(config.num_cache_lines);
    auto curr_base_cache_line_offset = align_to_power_of_floor(uniform_dis(gen), CACHELINE_SIZE);
    auto duration = std::chrono::duration<double>{0};
    for (size_t r = 0; r < config.num_resolves / config.num_cache_lines; ++r)
    {
        __builtin_prefetch(reinterpret_cast<void *>(reinterpret_cast<char *>(data.data()) + curr_base_cache_line_offset), 0, 0);

        lfence();
        asm volatile("" ::: "memory");
        auto start = std::chrono::steady_clock::now();
        asm volatile("" ::: "memory");

        for (size_t i = 0; i < config.num_cache_lines; ++i)
        {
            __builtin_prefetch(reinterpret_cast<void *>(reinterpret_cast<char *>(data.data()) + curr_base_cache_line_offset + CACHELINE_SIZE * i), 0, 0);
        }
        asm volatile("" ::: "memory");
        auto end = std::chrono::steady_clock::now();
        asm volatile("" ::: "memory");
        auto next_base_cache_line_offset = murmur_32(r);
        for (size_t i = 0; i < config.num_cache_lines; ++i)
        {
            for (size_t j = 0; j < CACHELINE_SIZE / sizeof(uint32_t); ++j)
            {
                for (size_t l = 0; l < COMPUTE_FACTOR; l++)
                {
                    next_base_cache_line_offset += murmur_32((*reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(data.data())) >> l + curr_base_cache_line_offset + CACHELINE_SIZE * i + sizeof(uint32_t) * j));
                }
            }
        }
        curr_base_cache_line_offset = next_base_cache_line_offset % (data_size_bytes - 1 - CACHELINE_SIZE * config.num_cache_lines);
        // std::cout << end - start << std::endl;
        duration += (end - start - CLOCK_MIN_DURATION);
        // LFB CLEAR
        // idle computation
        // noops
    }
    if (curr_base_cache_line_offset > data_size_bytes)
    {
        throw std::runtime_error("bad offset");
    }
    durations[thread_id] = duration;
    for (auto p : curr_pointers)
    {
        if (p >= data.size())
        {
            throw std::runtime_error("Invalid final pointer stored");
        }
    }
};

struct PCBenchmarkConfig
{
    size_t total_memory;
    size_t num_threads;
    size_t num_resolves;
    size_t num_parallel_pc;
    bool use_explicit_huge_pages;
    bool madvise_huge_pages;
};

void pointer_chase(size_t thread_id, auto &config, auto &data, auto &durations)
{
    pin_to_cpu(Prefetching::get().numa_manager.node_to_available_cpus[0][thread_id]);
    uint8_t dependency = 0;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> uniform_dis(0, data.size() - 1);

    std::vector<uint64_t> curr_pointers(config.num_parallel_pc);
    std::vector<uint64_t> next_pointers(config.num_parallel_pc);
    for (int i = 0; i < curr_pointers.size(); ++i)
    {
        curr_pointers[i] = align_to_power_of_floor(uniform_dis(gen), CACHELINE_SIZE);
    }
    auto duration = std::chrono::duration<double>{0};
    for (size_t r = 0; r < config.num_resolves; ++r)
    {
        for (size_t i = 0; i < config.num_parallel_pc - 1; ++i)
        {
            __builtin_prefetch(reinterpret_cast<void *>(data.data() + curr_pointers[i]), 0, 0);
        }
        auto start = std::chrono::high_resolution_clock::now();
        asm volatile("" ::: "memory");

        __builtin_prefetch(reinterpret_cast<void *>(data.data() + curr_pointers[config.num_parallel_pc - 1]), 0, 0);
        asm volatile("" ::: "memory");
        auto end = std::chrono::high_resolution_clock::now();
        auto rotation_offset = murmur_32(r);
        for (size_t i = config.num_parallel_pc; i > 0; --i)
        {
            next_pointers[i] = *(data.data() + curr_pointers[i]);
            for (size_t j = 0; j < CACHELINE_SIZE / sizeof(uint32_t); j++)
            {
                rotation_offset += murmur_32(*reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(data.data() + curr_pointers[i]) + (sizeof(uint32_t) * j)) + rotation_offset);
            }
        }
        rotation_offset %= config.num_parallel_pc;
        for (int i = 0; i < config.num_parallel_pc; ++i)
        {
            curr_pointers[i] = (next_pointers[(rotation_offset + i) % config.num_parallel_pc] + rotation_offset) % data.size();
        }
        duration += end - start - CLOCK_MIN_DURATION;
        // LFB CLEAR
        // idle computation
        // noops
    }
    durations[thread_id] = duration;
    for (auto p : curr_pointers)
    {
        if (p >= data.size())
        {
            throw std::runtime_error("Invalid final pointer stored");
        }
    }
};

std::pmr::vector<uint64_t> *cached_pointer_chase_arr = nullptr;
void lfb_size_benchmark(PBCBenchmarkConfig config, nlohmann::json &results)
{
    StaticNumaMemoryResource mem_res{0, config.use_explicit_huge_pages, config.madvise_huge_pages};
    std::random_device rd;
    std::mt19937 gen(rd());

    auto num_pointers = config.total_memory * 1024 * 1024 / sizeof(uint64_t); // memory given in MiB
    if (!(cached_pointer_chase_arr && (cached_pointer_chase_arr->size() == num_pointers)))
    {
        if (cached_pointer_chase_arr)
        {
            delete cached_pointer_chase_arr;
        }
        cached_pointer_chase_arr = new std::pmr::vector<uint64_t>(num_pointers, &mem_res);
        initialize_pointer_chase(cached_pointer_chase_arr->data(), cached_pointer_chase_arr->size());
    }
    auto pointer_chase_arr = *cached_pointer_chase_arr;

    std::vector<std::jthread> threads;

    // ---- Warm-up ----
    auto warm_up_config = PBCBenchmarkConfig{config};
    warm_up_config.num_cache_lines = 16;
    warm_up_config.num_resolves = 100'000;
    std::vector<std::chrono::duration<double>> empty_durations(config.num_threads);
    for (size_t i = 0; i < config.num_threads; ++i)
    {
        threads.emplace_back([&, i]()
                             { pointer_block_chase(i, config, pointer_chase_arr, empty_durations); });
    }
    for (auto &t : threads)
    {
        t.join();
    }
    threads.clear();
    // ---- End-up ----
    std::vector<std::chrono::duration<double>> durations(config.num_threads);
    for (size_t i = 0; i < config.num_threads; ++i)
    {
        threads.emplace_back([&, i]()
                             { pointer_block_chase(i, config, pointer_chase_arr, durations); });
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
    results["runtime"] = total_time.count();
    std::cout << config.num_cache_lines << " took " << total_time.count() << std::endl;
}

int main(int argc, char **argv)
{
    auto &benchmark_config = Prefetching::get().runtime_config;

    // clang-format off
    benchmark_config.add_options()
        ("total_memory", "Total memory allocated MiB", cxxopts::value<std::vector<size_t>>()->default_value("1024"))
        ("num_threads", "Number of threads running the bench", cxxopts::value<std::vector<size_t>>()->default_value("1"))
        ("num_resolves", "Number of resolves each pointer chase executes", cxxopts::value<std::vector<size_t>>()->default_value("100000"))
        ("num_parallel_pc", "Number of parallel pointer chases per thread", cxxopts::value<std::vector<size_t>>()->default_value("10"))
        ("use_explicit_huge_pages", "Use huge pages during allocation", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("madvise_huge_pages", "Madvise kernel to create huge pages on mem regions", cxxopts::value<std::vector<bool>>()->default_value("true"));
    // clang-format on
    benchmark_config.parse(argc, argv);

    int benchmark_run = 0;

    std::vector<nlohmann::json> all_results;
    for (auto &runtime_config : benchmark_config.get_runtime_configs())
    {
        auto total_memory = convert<size_t>(runtime_config["total_memory"]);
        auto num_threads = convert<size_t>(runtime_config["num_threads"]);
        auto num_resolves = convert<size_t>(runtime_config["num_resolves"]);
        auto num_parallel_pc = convert<size_t>(runtime_config["num_parallel_pc"]);
        auto use_explicit_huge_pages = convert<bool>(runtime_config["use_explicit_huge_pages"]);
        auto madvise_huge_pages = convert<bool>(runtime_config["madvise_huge_pages"]);
        PBCBenchmarkConfig config = {
            total_memory,
            num_threads,
            num_resolves,
            num_parallel_pc,
            use_explicit_huge_pages,
            madvise_huge_pages,
        };
        nlohmann::json results;
        results["config"]["total_memory"] = config.total_memory;
        results["config"]["num_threads"] = config.num_threads;
        results["config"]["num_resolves"] = config.num_resolves;
        results["config"]["num_cache_lines"] = config.num_cache_lines;
        results["config"]["use_explicit_huge_pages"] = config.use_explicit_huge_pages;
        results["config"]["madvise_huge_pages"] = config.madvise_huge_pages;

        lfb_size_benchmark(config, results);
        all_results.push_back(results);
        auto results_file = std::ofstream{"pc_benchmark.json"};
        nlohmann::json intermediate_json;
        intermediate_json["results"] = all_results;
        results_file << intermediate_json.dump(-1) << std::flush;
    }

    return 0;
}
