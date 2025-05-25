#include "prefetching.hpp"

#include <random>
#include <chrono>
#include <thread>
#include <iostream>
#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

#include "numa/numa_memory_resource.hpp"
#include "numa/numa_memory_resource_no_jemalloc.hpp"
#include "utils/utils.cpp"
#include "utils/stats.hpp"
#include "utils/simple_continuous_allocator.hpp"
#include "../../config.hpp"
#include "utils/fujitsu_memory_allocation.hpp"

const size_t CACHELINE_SIZE = get_cache_line_size();
const size_t REPEATS = 10;
const uintptr_t ca_weak_reliability_mask = uintptr_t(1) << 60;
struct ROBPressureBenchmarkConfig
{
    bool use_hash;
    size_t total_memory;
    size_t num_threads;
    size_t num_accesses;
    size_t num_instructions;
    std::string locality_hint;
    bool prefetch;
    bool run_remote_memory;
    NodeID run_on_node;
    NodeID alloc_on_node;
    bool use_explicit_huge_pages;
    bool madvise_huge_pages;
    bool reliability;
    bool profile;
};

template <int N>
struct Unroll_Nops
{
    static void run()
    {
        asm volatile("nop");
        Unroll_Nops<N - 1>::run();
    }
};

template <>
struct Unroll_Nops<0>
{
    static void run() {}
};

template <int N>
struct Unroll_Hash
{
    inline static long run(long x) // based on sdbm http://www.cse.yorku.ca/~oz/hash.html
    {
        long hash = Unroll_Hash<N - 1>::run(x);
        hash = x + (hash << 6) + (hash << 16) - hash;
        return hash;
    }
};

template <>
struct Unroll_Hash<0>
{
    inline static long run(long x) { return 0; }
};

template <int locality, int num_instructions, bool reliability>
void camel_nop(
    size_t i, size_t number_accesses,
    const ROBPressureBenchmarkConfig &config,
    auto &data, auto &accesses, auto &access_durations, auto &mt_event_counter)
{
    pin_to_cpu(Prefetching::get().numa_manager.node_to_available_cpus[config.run_on_node][i]);

    if (config.profile)
    {
        mt_event_counter.start(i);
    }
    size_t start_access = i * number_accesses;

    int dummy_dependency = 0; // data has only zeros written to it, so this will effectively do nothing, besides
                              // adding a data dependency - Hopefully this forces batches to be loaded sequentially.

    size_t b = 0;
    const bool prefetch = config.prefetch;
    const size_t prefetch_distance = 32;

    auto start = std::chrono::high_resolution_clock::now();
    for (int j = 0; j < config.num_accesses; ++j)
    {
        if (prefetch)
        {
            if constexpr (reliability)
            {
                __builtin_prefetch(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(data.data() + accesses[start_access + j + prefetch_distance]) | ca_weak_reliability_mask), 0, locality);
            }
            else
            {
                __builtin_prefetch(reinterpret_cast<void *>(data.data() + accesses[start_access + j + prefetch_distance]), 0, locality);
            }
        }
        Unroll_Nops<num_instructions>::run();
        dummy_dependency += data[accesses[start_access + j]];
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> access_duration = end - start;

    if (config.profile)
    {
        mt_event_counter.stop(i);
    }
    if (dummy_dependency != 0)
    {
        throw std::runtime_error("Dependency != 0. (Got : " + std::to_string(dummy_dependency) + " instead)");
    }
    access_durations[i] = access_duration / config.num_accesses;
};

template <int locality, int num_instructions, bool reliability>
void camel_hash(
    size_t i, size_t number_accesses,
    const ROBPressureBenchmarkConfig &config,
    auto &data, auto &accesses, auto &access_durations, auto &mt_event_counter)
{
    pin_to_cpu(Prefetching::get().numa_manager.node_to_available_cpus[config.run_on_node][i]);

    if (config.profile)
    {
        mt_event_counter.start(i);
    }
    size_t start_access = i * number_accesses;

    int dummy_dependency = 0; // data has only zeros written to it, so this will effectively do nothing, besides
                              // adding a data dependency - Hopefully this forces batches to be loaded sequentially.

    size_t b = 0;
    const bool prefetch = config.prefetch;
    const size_t prefetch_distance = 32;

    auto start = std::chrono::high_resolution_clock::now();
    for (int j = 0; j < config.num_accesses; ++j)
    {
        if (prefetch)
        {
            if constexpr (reliability)
            {
                __builtin_prefetch(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(data.data() + accesses[start_access + j + prefetch_distance]) | ca_weak_reliability_mask), 0, locality);
            }
            else
            {
                __builtin_prefetch(reinterpret_cast<void *>(data.data() + accesses[start_access + j + prefetch_distance]), 0, locality);
            }
        }
        dummy_dependency += Unroll_Hash<num_instructions>::run(data[accesses[start_access + j]]);
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> access_duration = end - start;

    if (config.profile)
    {
        mt_event_counter.stop(i);
    }
    if (dummy_dependency != 0)
    {
        throw std::runtime_error("Dependency != 0. (Got : " + std::to_string(dummy_dependency) + " instead)");
    }
    access_durations[i] = access_duration / config.num_accesses;
};

template <int locality, int num_instructions, bool reliability>
void benchmark_wrapper(ROBPressureBenchmarkConfig &config, nlohmann::json &results, auto &zero_data)
{

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, zero_data.size() - 1);

    std::vector<std::uint64_t> accesses(config.num_accesses);
    std::vector<double> measurement_prefetch_durations(REPEATS);
    std::vector<double> measurement_access_durations(REPEATS);

    auto &perf_manager = Prefetching::get().perf_manager;
    auto mt_event_counter = perf_manager.get_mt_event_counter(config.num_threads);
    for (size_t r = 0; r < REPEATS; r++)
    {
        size_t number_accesses_per_thread = config.num_accesses / config.num_threads;
        std::vector<std::jthread> threads;
        std::vector<std::chrono::duration<double>> access_durations(config.num_threads);

        std::generate(accesses.begin(), accesses.end(), [&]()
                      { return dis(gen); });

        for (size_t i = 0; i < config.num_threads; ++i)
        {
            if (config.use_hash)
            {
                threads.emplace_back([&, i]()
                                     { camel_hash<locality, num_instructions, reliability>(
                                           i, number_accesses_per_thread, config,
                                           zero_data, accesses, access_durations, mt_event_counter); });
            }
            else
            {
                threads.emplace_back([&, i]()
                                     { camel_nop<locality, num_instructions, reliability>(
                                           i, number_accesses_per_thread, config,
                                           zero_data, accesses, access_durations, mt_event_counter); });
            }
        }
        for (auto &t : threads)
        {
            t.join();
        }
        if (config.profile)
        {
            perf_manager.result(mt_event_counter, results, config.num_accesses);
        }
        threads.clear();
        auto total_access_time = std::chrono::duration<double>{0};
        for (auto &duration : access_durations)
        {
            total_access_time += duration;
        }
        measurement_access_durations[r] = (total_access_time / config.num_threads).count();
    }
    generate_stats(results, measurement_access_durations, "access_");

    std::cout << " locality: " << config.locality_hint << " prefetch: " << config.prefetch << std::endl;
    std::cout << "took " << results["access_runtime"] << std::endl;
}

template <int locality, int num_instructions, bool reliability>
std::function<void(ROBPressureBenchmarkConfig &, nlohmann::json &, std::pmr::vector<char> &)>
benchmark_return_func(ROBPressureBenchmarkConfig &config)
{
    return [](ROBPressureBenchmarkConfig &config, nlohmann::json &results, std::pmr::vector<char> &data)
    {
        benchmark_wrapper<locality, num_instructions, reliability>(config, results, data);
    };
}

template <int locality, bool reliability>
std::function<void(ROBPressureBenchmarkConfig &, nlohmann::json &, std::pmr::vector<char> &)>
benchmark_unpack_num_instructions(ROBPressureBenchmarkConfig &config)
{
#define BENCHMARK_CASE(locality, case_value, reliability) \
    case case_value:                                      \
        return benchmark_return_func<locality, case_value, reliability>(config);

    switch (config.num_instructions)
    {
        BENCHMARK_CASE(locality, 1, reliability)
        BENCHMARK_CASE(locality, 2, reliability)
        BENCHMARK_CASE(locality, 4, reliability)
        BENCHMARK_CASE(locality, 8, reliability)
        BENCHMARK_CASE(locality, 16, reliability)
        BENCHMARK_CASE(locality, 32, reliability)
        BENCHMARK_CASE(locality, 64, reliability)
        BENCHMARK_CASE(locality, 128, reliability)
        BENCHMARK_CASE(locality, 256, reliability)
        BENCHMARK_CASE(locality, 512, reliability)
        BENCHMARK_CASE(locality, 1024, reliability)
        // also add for base 1.3:
        BENCHMARK_CASE(locality, 3, reliability)
        BENCHMARK_CASE(locality, 5, reliability)
        BENCHMARK_CASE(locality, 6, reliability)
        BENCHMARK_CASE(locality, 11, reliability)
        BENCHMARK_CASE(locality, 14, reliability)
        BENCHMARK_CASE(locality, 18, reliability)
        BENCHMARK_CASE(locality, 24, reliability)
        BENCHMARK_CASE(locality, 31, reliability)
        BENCHMARK_CASE(locality, 41, reliability)
        BENCHMARK_CASE(locality, 53, reliability)
        BENCHMARK_CASE(locality, 69, reliability)
        BENCHMARK_CASE(locality, 90, reliability)
        BENCHMARK_CASE(locality, 118, reliability)
        BENCHMARK_CASE(locality, 153, reliability)
        BENCHMARK_CASE(locality, 199, reliability)
        BENCHMARK_CASE(locality, 259, reliability)
        BENCHMARK_CASE(locality, 337, reliability)
        BENCHMARK_CASE(locality, 438, reliability)
        BENCHMARK_CASE(locality, 570, reliability)
        BENCHMARK_CASE(locality, 741, reliability)
        BENCHMARK_CASE(locality, 963, reliability)
        BENCHMARK_CASE(locality, 1252, reliability)
        BENCHMARK_CASE(locality, 1628, reliability)
        BENCHMARK_CASE(locality, 2115, reliability)
    default:
        throw std::runtime_error("num_instructions not defined. Got: " + std::to_string(config.num_instructions));
    }
}

template <int locality>
std::function<void(ROBPressureBenchmarkConfig &, nlohmann::json &, std::pmr::vector<char> &)>
benchmark_unpack_reliability(ROBPressureBenchmarkConfig &config)
{
    if (config.reliability)
    {
        return benchmark_unpack_num_instructions<0, true>(config);
    }
    else
    {
        return benchmark_unpack_num_instructions<0, false>(config);
    }
}

std::function<void(ROBPressureBenchmarkConfig &, nlohmann::json &, std::pmr::vector<char> &)>
benchmark_unpack_config(ROBPressureBenchmarkConfig &config)
{
    if (config.locality_hint == "NTA")
    {
        // _MM_HINT_NTA
        return benchmark_unpack_reliability<0>(config);
    }
    // else if (config.locality_hint == "T0")
    //{
    //     // _MM_HINT_T0
    //     return benchmark_unpack_reliability<3>(config);
    // }
    // else if (config.locality_hint == "T1")
    //{
    //     // _MM_HINT_T1
    //     return benchmark_unpack_reliability<2>(config);
    // }
    // else if (config.locality_hint == "T2")
    //{
    //     // _MM_HINT_T2
    //     return benchmark_unpack_reliability<1>(config);
    // }
    else
    {
        throw std::runtime_error("Unknown locality_hint given: " + config.locality_hint);
    }
}

int main(int argc, char **argv)
{
    auto &benchmark_config = Prefetching::get().runtime_config;
    auto &perf_manager = Prefetching::get().perf_manager;
    perf_manager.initialize_counter_definition(get_default_perf_config_file());

    // clang-format off
    benchmark_config.add_options()
        ("use_hash", "Uses hash function creating a real data dependency (Original Camel bench introduced by Kwon).", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("total_memory", "Total memory allocated MiB", cxxopts::value<std::vector<size_t>>()->default_value("512"))
        ("num_threads", "Number of threads running the bench", cxxopts::value<std::vector<size_t>>()->default_value("1"))
        ("num_accesses", "Number of accesses", cxxopts::value<std::vector<size_t>>()->default_value("10000000"))
        ("num_instructions", "Number of instructions per resolve", cxxopts::value<std::vector<size_t>>()->default_value("1,2,4,8,16,32,64,128,256,512,1024"))
        ("locality_hint", "locality_hint, can be nta, t0, t1, or t2", cxxopts::value<std::vector<std::string>>()->default_value("NTA"))
        ("prefetch", "Prefetch before accessing", cxxopts::value<std::vector<bool>>()->default_value("true,false"))
        ("run_remote_memory", "Attempts to load remote memory Numa Setting from config.", cxxopts::value<std::vector<bool>>()->default_value("true,false"))
        ("run_on_node", "Which NUMA node to run the benchmark on.", cxxopts::value<std::vector<NodeID>>()->default_value("0"))
        ("alloc_on_node", "Which NUMA node to alloc memory on.", cxxopts::value<std::vector<NodeID>>()->default_value("0"))
        ("use_explicit_huge_pages", "Use huge pages during allocation", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("madvise_huge_pages", "Madvise kernel to create huge pages on mem regions", cxxopts::value<std::vector<bool>>()->default_value("true"))
        ("reliability", "Fujitsu feature to tag address (true => weak, false => strong)", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("profile", "Profiles the execution. Sets repeat_lookup_measurement to 1.", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("out", "Path on which results should be stored", cxxopts::value<std::vector<std::string>>()->default_value("rob_pressure.json"));
    // clang-format on
    benchmark_config.parse(argc, argv);

    int benchmark_run = 0;

    std::vector<nlohmann::json> all_results;
    for (auto &runtime_config : benchmark_config.get_runtime_configs())
    {
        auto use_hash = convert<bool>(runtime_config["use_hash"]);
        auto total_memory = convert<size_t>(runtime_config["total_memory"]);
        auto num_threads = convert<size_t>(runtime_config["num_threads"]);
        auto num_accesses = convert<size_t>(runtime_config["num_accesses"]);
        auto num_instructions = convert<size_t>(runtime_config["num_instructions"]);
        auto locality_hint = convert<std::string>(runtime_config["locality_hint"]);
        auto prefetch = convert<bool>(runtime_config["prefetch"]);
        auto run_remote_memory = convert<bool>(runtime_config["run_remote_memory"]);
        auto run_on_node = convert<NodeID>(runtime_config["run_on_node"]);
        auto alloc_on_node = convert<NodeID>(runtime_config["alloc_on_node"]);
        auto use_explicit_huge_pages = convert<bool>(runtime_config["use_explicit_huge_pages"]);
        auto madvise_huge_pages = convert<bool>(runtime_config["madvise_huge_pages"]);
        auto reliability = convert<bool>(runtime_config["reliability"]);
        auto profile = convert<bool>(runtime_config["profile"]);
        auto out = convert<std::string>(runtime_config["out"]);

        if (reliability && !get_curr_hostname().starts_with("ca"))
        {
            // Ignore reliability = True on non Fujitsu nodes
            continue;
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
        ROBPressureBenchmarkConfig config = {
            use_hash,
            total_memory,
            num_threads,
            num_accesses,
            num_instructions,
            locality_hint,
            prefetch,
            run_remote_memory,
            run_on_node,
            alloc_on_node,
            use_explicit_huge_pages,
            madvise_huge_pages,
            reliability,
            profile,
        };

        auto total_memory_bytes = config.total_memory * 1024 * 1024; // memory given in MiB
        if (Prefetching::get().numa_manager.node_to_available_cpus[config.alloc_on_node].size() > 0)
        {
            pin_to_cpus(Prefetching::get().numa_manager.node_to_available_cpus[config.alloc_on_node]);
        }
        NumaMemoryResourceNoJemalloc mem_res{config.alloc_on_node, config.use_explicit_huge_pages, config.madvise_huge_pages};
        SimpleContinuousAllocator simple_continuous_allocator{mem_res, 1024l * (1 << 20), 512l * (1 << 20)};
        std::pmr::vector<char> data(total_memory_bytes, &simple_continuous_allocator);

        nlohmann::json results;
        results["config"]["use_hash"] = config.use_hash;
        results["config"]["total_memory"] = config.total_memory;
        results["config"]["num_threads"] = config.num_threads;
        results["config"]["num_accesses"] = config.num_accesses;
        results["config"]["num_instructions"] = config.num_instructions;
        results["config"]["locality_hint"] = config.locality_hint;
        results["config"]["prefetch"] = config.prefetch;
        results["config"]["run_remote_memory"] = config.run_remote_memory;
        results["config"]["run_on_node"] = config.run_on_node;
        results["config"]["alloc_on_node"] = config.alloc_on_node;
        results["config"]["use_explicit_huge_pages"] = config.use_explicit_huge_pages;
        results["config"]["madvise_huge_pages"] = config.madvise_huge_pages;
        results["config"]["reliability"] = config.reliability;
        results["config"]["profile"] = config.profile;

        benchmark_unpack_config(config)(config, results, data);

        all_results.push_back(results);

        auto results_file = std::ofstream{out};
        nlohmann::json intermediate_json;
        intermediate_json["results"] = all_results;
        results_file << intermediate_json.dump(-1) << std::flush;
    }

    return 0;
}