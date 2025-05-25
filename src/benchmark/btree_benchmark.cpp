#include "prefetching.hpp"

#include <random>
#include <chrono>
#include <assert.h>
#include <numa.h>
#include <numaif.h>
#include <sys/sysinfo.h>
#include <thread>
#include <iostream>
#include <fstream>

#include <nlohmann/json.hpp>
#include "utils/zipfian_int_distribution.hpp"
#include "utils/stats.hpp"
#include "../lib/utils/utils.hpp"
#include "../lib/BTree/btree_olc.h"
#include "../lib/BTree/coro_base_btree_olc.h"
#include "../lib/BTree/coro_btree_olc.h"
#include "../lib/BTree/coro_btree_olc_optimized.h"
#include "../lib/BTree/coro_lines_btree_olc.h"
#include "../lib/BTree/btree_vectorized_helper.h"
#include "numa/numa_memory_resource_no_jemalloc.hpp"
#include "../lib/utils/simple_continuous_allocator.hpp"
#include "../../config.hpp"

uintptr_t SWPrefetcher::reliability_mask = 0;
struct BTreeBenchmarkConfig
{
    size_t tree_node_size;
    size_t num_threads;
    size_t coroutines;
    size_t num_lookups;
    size_t num_elements;
    size_t repeat_lookup_measurement;
    bool run_remote_memory;
    NodeID run_on_node;
    NodeID alloc_on_node;
    std::string BTree_variant;
    std::string key_distribution;
    bool use_explicit_huge_pages;
    bool madvise_huge_pages;
    bool reliability;
    bool profile;
};

void log_system_resources()
{
    struct sysinfo info;
    if (sysinfo(&info) == 0)
    {
        std::cout << "Available RAM: " << (info.freeram / 1024 / 1024) << " MB\n";
        std::cout << "Number of CPU cores: " << std::thread::hardware_concurrency() << "\n";
        std::cout << "Load averages: " << info.loads[0] << ", " << info.loads[1] << ", " << info.loads[2] << "\n";
        std::cout << "Concurrently executable threads: " << std::thread::hardware_concurrency() << "\n";
    }
    else
    {
        std::cerr << "Failed to retrieve system info.\n";
    }
}

template <typename BTree>
void build_tree(
    unsigned thread_id, const size_t offset, const size_t inserts_per_thread, const BTreeBenchmarkConfig &config,
    BTree &btree,
    auto &kv_pairs)
{
    try
    {

        if constexpr (has_task_type<BTree>::value)
        {
            co_insert(offset, offset + inserts_per_thread, 1, btree, kv_pairs);
        }
        else if constexpr (has_optimized_task_type<BTree>::value)
        {
            co_insert_optimized(offset, offset + inserts_per_thread, 1, btree, kv_pairs);
        }
        else
        {
            vec_insert(offset, offset + inserts_per_thread, btree, kv_pairs);
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error in build_tree for thread " << thread_id << ": " << e.what() << std::endl;
    }
}

template <typename BTree>
void benchmark_btree_lookups(
    unsigned thread_id, const BTreeBenchmarkConfig &config,
    BTree &btree,
    auto &kv_pairs,
    std::atomic<bool> &start_lookups,
    auto &event_counter)
{
    try
    {
        pin_to_cpu(Prefetching::get().numa_manager.node_to_available_cpus[config.run_on_node][thread_id]);

        if (config.profile)
        {
            event_counter.start(thread_id);
        }

        const size_t lookups_per_thread = config.num_lookups / config.num_threads;
        const size_t offset = lookups_per_thread * thread_id;

        while (!start_lookups)
        {
            wait_cycles(100);
        }

        if constexpr (has_task_type<BTree>::value)
        {
            schedule_coroutines<BTree>(offset, offset + lookups_per_thread, config.coroutines, btree, kv_pairs);
        }
        else if constexpr (has_optimized_task_type<BTree>::value)
        {
            schedule_coroutines_optimized<BTree>(offset, offset + lookups_per_thread, config.coroutines, btree, kv_pairs);
        }
        else
        {
            vectorized_get<BTree>(offset, offset + lookups_per_thread, btree, kv_pairs);
        }
        if (config.profile)
        {
            event_counter.stop(thread_id);
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error in benchmark_btree_lookups for thread " << thread_id << ": " << e.what() << std::endl;
    }
}

template <typename BTree>
void benchmark_wrapper(BTreeBenchmarkConfig &config, nlohmann::json &results)
{
    std::vector<std::jthread> threads;

    if (Prefetching::get().numa_manager.node_to_available_cpus[config.alloc_on_node].size() > 0)
    {
        pin_to_cpus(Prefetching::get().numa_manager.node_to_available_cpus[config.alloc_on_node]);
    }
    NumaMemoryResourceNoJemalloc mem_res{config.alloc_on_node, config.use_explicit_huge_pages, config.madvise_huge_pages};
    SimpleContinuousAllocator allocator(mem_res, 2048l * (1 << 20), 512l * (1 << 20), get_curr_hostname().starts_with("ca"));
    //       === BUILD PHASE ===
    std::vector<std::pair<std::uint64_t, std::uint64_t>>
        kv_pairs;
    kv_pairs.reserve(config.num_elements);

    for (unsigned i = 0; i < config.num_elements; ++i)
    {
        kv_pairs.emplace_back(i, i + 1);
    }

    SWPrefetcher::reliability_mask = (config.reliability) ? uintptr_t(1) << 60 : 0;
    BTree btree{allocator};

    auto start_build = std::chrono::high_resolution_clock::now();
    size_t build_threads = 16;
    size_t base_inserts_per_thread = config.num_elements / build_threads;
    size_t remainder = config.num_elements % build_threads;
    for (size_t t = 0; t < build_threads; ++t)
    {
        size_t inserts_per_thread = base_inserts_per_thread;

        if (t < remainder)
        {
            ++inserts_per_thread; // Give one extra element to the first `remainder` threads
        }

        size_t offset = t * base_inserts_per_thread + std::min(t, remainder);

        threads.emplace_back([&, t, inserts_per_thread, offset]()
                             { build_tree(t, offset, inserts_per_thread, config, btree, kv_pairs); });
    }

    for (auto &t : threads)
    {
        t.join();
    }
    threads.clear();
    auto end_build = std::chrono::high_resolution_clock::now();
    auto build_runtime = std::chrono::duration<double>(end_build - start_build).count();
    results["build_runtime"] = build_runtime;

    //       === LOOKUP PHASE ===

    std::random_device rd;
    std::mt19937 gen(rd());

    zipfian_int_distribution<int>::param_type p(0, config.num_elements - 1, 0.99);
    zipfian_int_distribution<int> zipfian_dis(p);
    std::uniform_int_distribution<std::uint64_t> uniform_dis(0, config.num_elements - 1);

    auto &perf_manager = Prefetching::get().perf_manager;
    auto mt_event_counter = perf_manager.get_mt_event_counter(config.num_threads);
    kv_pairs.clear();
    kv_pairs.reserve(config.num_lookups);
    for (unsigned i = 0; i < config.num_lookups; i++)
    {
        uint64_t random_key;
        if (config.key_distribution == "uniform")
        {
            random_key = uniform_dis(gen);
        }
        else if (config.key_distribution == "zip")
        {
            random_key = zipfian_dis(gen);
        }
        else
        {
            throw std::runtime_error("Unknown key_distribution encountered: " + config.key_distribution);
        }
        kv_pairs.emplace_back(random_key, random_key + 1);
    }

    std::vector<double> durations(config.repeat_lookup_measurement);
    for (unsigned measurement_id = 0; measurement_id < config.repeat_lookup_measurement; measurement_id++)
    {
        std::shuffle(kv_pairs.begin(), kv_pairs.end(), gen);
        std::atomic<bool> start_lookups = false;

        for (size_t t = 0; t < config.num_threads; ++t)
        {
            threads.emplace_back([&, t]()
                                 { benchmark_btree_lookups(t, config, btree, kv_pairs, start_lookups, mt_event_counter); });
        }

        auto start = std::chrono::high_resolution_clock::now();
        start_lookups = true;

        for (auto &t : threads)
        {
            t.join();
        }
        threads.clear();
        auto end = std::chrono::high_resolution_clock::now();
        if (config.profile)
        {
            perf_manager.result(mt_event_counter, results, config.num_lookups);
        }
        auto lookup_runtime = std::chrono::duration<double>(end - start).count();
        durations[measurement_id] = lookup_runtime;
    }
    generate_stats(results, durations, "lookup_");
    std::cout << config.BTree_variant << ";" << config.tree_node_size << "B;" << config.key_distribution << " lookup took: " << results["lookup_runtime"] << " seconds" << std::endl;
}

template <const size_t node_size>
void run_benchmark_cacheline_size(BTreeBenchmarkConfig &config, nlohmann::json &results)
{
    const size_t cache_line_size = get_cache_line_size();
    if (cache_line_size == 64)
    {
        run_benchmark_variant<node_size, 64>(config, results);
    }
    else if (cache_line_size == 256)
    {
        run_benchmark_variant<node_size, 256>(config, results);
    }
    else
    {
        throw std::runtime_error("Invalid cache-line-size encountered: " + std::to_string(cache_line_size));
    }
}

template <const size_t node_size, const size_t cache_line_size>
void run_benchmark_variant(BTreeBenchmarkConfig &config, nlohmann::json &results)
{
    const uintptr_t reliability_mask = (config.reliability) ? uintptr_t(1) << 60 : 0;
    if (config.BTree_variant == "normal")
    {
        benchmark_wrapper<btreeolc::BTree<std::uint64_t, std::uint64_t, node_size>>(config, results);
    }
    else if (config.BTree_variant == "coro_full_node")
    {
        benchmark_wrapper<btreeolc::coro_base::BTree<std::uint64_t, std::uint64_t, node_size, cache_line_size>>(config, results);
    }
    else if (config.BTree_variant == "coro_half_node")
    {
        benchmark_wrapper<btreeolc::coro::BTree<std::uint64_t, std::uint64_t, node_size, cache_line_size>>(config, results);
    }
    else if (config.BTree_variant == "coro_half_node_optimized")
    {
        benchmark_wrapper<btreeolc::coro_optimized::BTree<std::uint64_t, std::uint64_t, node_size, cache_line_size>>(config, results);
    }
    else if (config.BTree_variant == "coro_lines_node")
    {
        benchmark_wrapper<btreeolc::coro_lines::BTree<std::uint64_t, std::uint64_t, node_size, cache_line_size>>(config, results);
    }
    else
    {
        std::cerr << "Unknown BTree variant: " << config.BTree_variant << std::endl;
    }
}

int main(int argc, char **argv)
{
    auto &benchmark_config = Prefetching::get().runtime_config;
    auto &perf_manager = Prefetching::get().perf_manager;
    perf_manager.initialize_counter_definition(get_default_perf_config_file());

    // clang-format off
    benchmark_config.add_options()
        ("tree_node_size", "Tree Node size in Bytes", cxxopts::value<std::vector<size_t>>()->default_value("512"))
        ("num_threads", "Number of num_threads", cxxopts::value<std::vector<size_t>>()->default_value("1"))
        ("num_lookups", "Number of lookups", cxxopts::value<std::vector<size_t>>()->default_value("5000000"))
        ("num_elements", "Number of elements to fill the BTree with", cxxopts::value<std::vector<size_t>>()->default_value("50000000"))
        ("coroutines", "Number of coroutines per thread", cxxopts::value<std::vector<size_t>>()->default_value("20"))
        ("repeat_lookup_measurement", "Number of times the lookup benchmark shall be repeated", cxxopts::value<std::vector<size_t>>()->default_value("5"))
        ("run_remote_memory", "Attempts to load remote memory NumaSetting from config.", cxxopts::value<std::vector<bool>>()->default_value("true,false"))
        ("run_on_node", "Which NUMA node to run the benchmark on.", cxxopts::value<std::vector<NodeID>>()->default_value("0"))
        ("alloc_on_node", "Which NUMA node to alloc memory on.", cxxopts::value<std::vector<NodeID>>()->default_value("0"))
        ("btree_variant", "Which BTree to use (normal,coro_full_node,coro_half_node,coro_half_node_optimized,coro_lines_node)", cxxopts::value<std::vector<std::string>>()->default_value("normal,coro_full_node,coro_half_node,coro_lines_node"))
        ("key_distribution", "Kind of key distribution used for lookups (uniform, zip)", cxxopts::value<std::vector<std::string>>()->default_value("uniform"))
        ("use_explicit_huge_pages", "Use huge pages during allocation", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("madvise_huge_pages", "Madvise kernel to create huge pages on mem regions", cxxopts::value<std::vector<bool>>()->default_value("true"))
        ("reliability", "Fujitsu feature true -> weak reliability, else strong", cxxopts::value<std::vector<bool>>()->default_value("false,true"))
        ("profile", "Profiles the execution. Sets repeat_lookup_measurement to 1.", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("print_system_stats", "Print basic systems stats after each benchmark run.", cxxopts::value<std::vector<bool>>()->default_value("true"))
        ("out", "filename", cxxopts::value<std::vector<std::string>>()->default_value("btree_benchmark.json"));
    // clang-format on
    benchmark_config.parse(argc, argv);

    int benchmark_run = 0;
    for (auto &runtime_config : benchmark_config.get_runtime_configs())
    {
        auto tree_node_size = convert<size_t>(runtime_config["tree_node_size"]);
        auto num_threads = convert<size_t>(runtime_config["num_threads"]);
        auto coroutines = convert<size_t>(runtime_config["coroutines"]);
        auto num_lookups = convert<size_t>(runtime_config["num_lookups"]);
        auto num_elements = convert<size_t>(runtime_config["num_elements"]);
        auto repeat_lookup_measurement = convert<size_t>(runtime_config["repeat_lookup_measurement"]);
        auto run_remote_memory = convert<bool>(runtime_config["run_remote_memory"]);
        auto run_on_node = convert<NodeID>(runtime_config["run_on_node"]);
        auto alloc_on_node = convert<NodeID>(runtime_config["alloc_on_node"]);
        auto btree_variant = convert<std::string>(runtime_config["btree_variant"]);
        auto key_distribution = convert<std::string>(runtime_config["key_distribution"]);
        auto use_explicit_huge_pages = convert<bool>(runtime_config["use_explicit_huge_pages"]);
        auto madvise_huge_pages = convert<bool>(runtime_config["madvise_huge_pages"]);
        auto reliability = convert<bool>(runtime_config["reliability"]);
        auto profile = convert<bool>(runtime_config["profile"]);
        auto output_file = convert<std::string>(runtime_config["out"]);

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
        if (profile)
        {
            repeat_lookup_measurement = 1;
        }

        BTreeBenchmarkConfig config =
            {
                tree_node_size,
                num_threads,
                coroutines,
                num_lookups,
                num_elements,
                repeat_lookup_measurement,
                run_remote_memory,
                run_on_node,
                alloc_on_node,
                btree_variant,
                key_distribution,
                use_explicit_huge_pages,
                madvise_huge_pages,
                reliability,
                profile,
            };

        nlohmann::json results;
        results["config"]["tree_node_size"] = config.tree_node_size;
        results["config"]["num_threads"] = config.num_threads;
        results["config"]["coroutines"] = config.coroutines;
        results["config"]["num_lookups"] = config.num_lookups;
        results["config"]["num_elements"] = config.num_elements;
        results["config"]["run_remote_memory"] = config.run_remote_memory;
        results["config"]["run_on_node"] = config.run_on_node;
        results["config"]["alloc_on_node"] = config.alloc_on_node;
        results["config"]["repeat_lookup_measurement"] = config.repeat_lookup_measurement;
        results["config"]["btree_variant"] = config.BTree_variant;
        results["config"]["key_distribution"] = config.key_distribution;
        results["config"]["use_explicit_huge_pages"] = config.use_explicit_huge_pages;
        results["config"]["madvise_huge_pages"] = config.madvise_huge_pages;
        results["config"]["reliability"] = config.reliability;
        results["config"]["profile"] = config.profile;

        switch (config.tree_node_size)
        {
        case 128:
            run_benchmark_cacheline_size<128>(config, results);
            break;
        case 256:
            run_benchmark_cacheline_size<256>(config, results);
            break;
        case 512:
            run_benchmark_cacheline_size<512>(config, results);
            break;
        case 1024:
            run_benchmark_cacheline_size<1024>(config, results);
            break;
        case 2048:
            run_benchmark_cacheline_size<2048>(config, results);
            break;
        case 4096:
            run_benchmark_cacheline_size<4096>(config, results);
            break;
        case 8192:
            run_benchmark_cacheline_size<8192>(config, results);
            break;
        case 16384:
            run_benchmark_cacheline_size<16384>(config, results);
            break;
        case 32768:
            run_benchmark_cacheline_size<32768>(config, results);
            break;
        case 65536:
            run_benchmark_cacheline_size<65536>(config, results);
            break;
        case 131072:
            run_benchmark_cacheline_size<131072>(config, results);
            break;
        default:
            std::cerr << "Invalid tree_node_size: " << config.tree_node_size << ". Valid sizes are 256, 512, 1024, 4096, 8192, 16384, and 32768." << std::endl;
            break;
        }

        nlohmann::json output_json;
        if (std::filesystem::exists(output_file))
        {
            std::ifstream f(output_file);
            if (f.is_open())
            {
                output_json = nlohmann::json::parse(f, nullptr, false);
                f.close();
            }
        }

        if (output_json.is_null())
        {
            output_json = {{"results", std::vector<nlohmann::json>{}}};
        }
        output_json["results"].push_back(results);
        auto results_file = std::ofstream{output_file};
        results_file << output_json.dump(-1) << std::flush;
        if (convert<bool>(runtime_config["print_system_stats"]))
        {
            log_system_resources();
        }
    }

    return 0;
}