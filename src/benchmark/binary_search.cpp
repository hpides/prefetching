#include "prefetching.hpp"

#include <random>
#include <chrono>
#include <assert.h>
#include <numa.h>
#include <numaif.h>
#include <map>
#include <memory>
#include <sys/sysinfo.h>
#include <thread>
#include <span>
#include <iostream>
#include <fstream>

#include <nlohmann/json.hpp>
#include "utils/zipfian_int_distribution.hpp"
#include "utils/stats.hpp"
#include "../lib/utils/utils.hpp"
#include "../lib/BinarySearch/coro.h"
#include "../lib/BinarySearch/naive.h"
#include "../lib/BinarySearch/sm.h"
#include "../lib/utils/simple_continuous_allocator.hpp"
#include "../../config.hpp"
#include "numa/numa_memory_resource_no_jemalloc.hpp"

std::map<NodeID, std::shared_ptr<SimpleContinuousAllocator>> allocator_cache;
std::map<NodeID, std::shared_ptr<NumaMemoryResourceNoJemalloc>> memres_cache;

struct BinarySearchBenchmarkConfig
{
    size_t num_threads;
    size_t parallel_streams;
    size_t num_lookups;
    size_t num_elements;
    size_t repeat_lookup_measurement;
    bool run_remote_memory;
    NodeID run_on_node;
    NodeID alloc_on_node;
    std::string binary_search_variant;
    std::string key_distribution;
    bool reliability;
    bool profile;
    double zipf_theta;
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

void benchmark_binary_search_lookups(
    unsigned thread_id, const BinarySearchBenchmarkConfig &config,
    std::pmr::vector<int> &sorted_array,
    auto &lookups,
    std::atomic<bool> &start_lookups,
    auto lookup_func,
    auto &mt_event_counter)
{
    try
    {
        pin_to_cpu(Prefetching::get().numa_manager.node_to_available_cpus[config.run_on_node][thread_id]);

        if (config.profile)
        {
            mt_event_counter.start(thread_id);
        }
        const size_t lookups_per_thread = config.num_lookups / config.num_threads;
        const size_t offset = lookups_per_thread * thread_id;
        auto const my_lookups = std::span{lookups}.subspan(offset, lookups_per_thread);

        while (!start_lookups)
        {
            wait_cycles(100);
        }

        lookup_func(sorted_array, my_lookups, config.parallel_streams);
        if (config.profile)
        {
            mt_event_counter.stop(thread_id);
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error in benchmark_binary_search_lookups for thread " << thread_id << ": " << e.what() << std::endl;
    }
}

template <const size_t cache_line_size>
void benchmark_wrapper(BinarySearchBenchmarkConfig &config, nlohmann::json &results, auto lookup_func)
{
    std::vector<std::jthread> threads;
    if (get_curr_hostname().starts_with("gx0"))
    {
        pin_to_cpus(Prefetching::get().numa_manager.node_to_available_cpus[config.run_on_node]);
    }
    else
    {
        if (Prefetching::get().numa_manager.node_to_available_cpus[config.alloc_on_node].size() > 0)
        {
            pin_to_cpus(Prefetching::get().numa_manager.node_to_available_cpus[config.alloc_on_node]);
        }
    }

    // NumaMemoryResourceNoJemalloc mem_res{config.alloc_on_node, false, true};
    // SimpleContinuousAllocator allocator(mem_res, 2048l * (1 << 20), 512l * (1 << 20), get_curr_hostname().starts_with("ca"));

    if (!allocator_cache.contains(config.alloc_on_node))
    {
        auto memres = std::make_shared<NumaMemoryResourceNoJemalloc>(config.alloc_on_node, false, true);
        memres_cache[config.alloc_on_node] = memres;
        auto allocator_shared = std::make_shared<SimpleContinuousAllocator>(*memres, 2048l * (1 << 20), 512l * (1 << 20), get_curr_hostname().starts_with("ca") || get_curr_hostname().starts_with("gx0"));
        allocator_cache[config.alloc_on_node] = allocator_shared;
    }

    auto allocator = allocator_cache[config.alloc_on_node];

    std::vector<double> durations(config.repeat_lookup_measurement);
    for (unsigned measurement_id = 0; measurement_id < config.repeat_lookup_measurement; measurement_id++)
    {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        //        === BUILD PHASE ===
        allocator->clear_all_allocated_regions();
        std::pmr::vector<int> sorted_array(config.num_elements, allocator.get());
        // "dense binary search"
        std::iota(sorted_array.begin(), sorted_array.end(), 0);

        if (Prefetching::get().numa_manager.node_to_available_cpus[config.run_on_node].size() > 0)
        {
            pin_to_cpus(Prefetching::get().numa_manager.node_to_available_cpus[config.run_on_node]);
        }
        std::random_device rd;
        std::mt19937 gen(rd());

        //       === LOOKUP PHASE ===
        zipfian_int_distribution<int>::param_type p(0, config.num_elements - 1, config.zipf_theta); // TODO: this is slow, make fast

        zipfian_int_distribution<int> zipfian_dis(p);
        std::uniform_int_distribution<std::uint64_t> uniform_dis(0, config.num_elements - 1);

        auto &perf_manager = Prefetching::get().perf_manager;
        auto mt_event_counter = perf_manager.get_mt_event_counter(config.num_threads);

        std::vector<int> lookups;
        lookups.reserve(config.num_lookups);
        for (unsigned i = 0; i < config.num_lookups; i++)
        {
            uint64_t random_key;
            if (config.key_distribution == "uniform")
            {
                lookups.push_back(uniform_dis(gen));
            }
            else if (config.key_distribution == "zip")
            {
                lookups.push_back(zipfian_dis(gen));
            }
            else
            {
                throw std::runtime_error("Unknown key_distribution encountered: " + config.key_distribution);
            }
        }

        std::shuffle(lookups.begin(), lookups.end(), gen);
        std::atomic<bool> start_lookups = false;
        for (size_t t = 0; t < config.num_threads; ++t)
        {
            threads.emplace_back([&, t]()
                                 { benchmark_binary_search_lookups(t, config, sorted_array,
                                                                   lookups, start_lookups,
                                                                   lookup_func, mt_event_counter); });
        }

        auto start = std::chrono::high_resolution_clock::now();
        start_lookups = true;

        for (auto &t : threads)
        {
            t.join();
        }
        threads.clear();
        auto end = std::chrono::high_resolution_clock::now();
        auto lookup_runtime = std::chrono::duration<double>(end - start).count();
        durations[measurement_id] = lookup_runtime;
        if (config.profile)
        {
            perf_manager.result(mt_event_counter, results, config.num_lookups);
        }
    }
    generate_stats(results, durations, "lookup_");
    std::cout << config.binary_search_variant << ";" << config.key_distribution << ";remote_memory:" << config.run_remote_memory << ";parallel_streams:" << config.parallel_streams << " lookup took: " << results["lookup_runtime"] << " seconds" << std::endl;
}

std::function<long(const std::pmr::vector<int> &, const std::span<int> &, int)>
get_func(BinarySearchBenchmarkConfig &config)
{
    if (config.binary_search_variant == "naive")
    {
        return [](std::pmr::vector<int> const &v, std::span<int> const &lookups, int streams)
        {
            long found = 0;
            auto beg = v.begin();
            auto end = v.end();
            for (int key : lookups)
                if (naive_binary_search(beg, end, key))
                    ++found;
            return found;
        };
    }
    else if (config.binary_search_variant == "coro")
    {
        return [&](std::pmr::vector<int> const &v, std::span<int> const &lookups, int streams)
        {
            if (config.reliability)
            {
                return CoroMultiLookup<true>(v, lookups, streams);
            }
            else
            {
                return CoroMultiLookup<false>(v, lookups, streams);
            }
        };
    }
    else if (config.binary_search_variant == "state")
    {
        return [&](std::pmr::vector<int> const &v, std::span<int> const &lookups, int streams)
        {
            if (config.reliability)
            {
                return SmMultiLookup<true>(v, lookups, streams);
            }
            else
            {
                return SmMultiLookup<false>(v, lookups, streams);
            }
        };
    }

    throw std::runtime_error("Unknown binary search variant: " + config.binary_search_variant);
}

template <const size_t cache_line_size>
void run_benchmark_variant(BinarySearchBenchmarkConfig &config, nlohmann::json &results)
{
    benchmark_wrapper<cache_line_size>(config, results, get_func(config));
}

void run_benchmark_cacheline_size(BinarySearchBenchmarkConfig &config, nlohmann::json &results)
{
    const size_t cache_line_size = get_cache_line_size();
    if (cache_line_size == 64)
    {
        run_benchmark_variant<64>(config, results);
    }
    else if (cache_line_size == 256)
    {
        run_benchmark_variant<256>(config, results);
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
        ("parallel_streams", "Number of parallel streams per thread", cxxopts::value<std::vector<size_t>>()->default_value("20"))
        ("num_lookups", "Number of lookups", cxxopts::value<std::vector<size_t>>()->default_value("5000000"))
        ("num_elements", "Number of elements to fill the BTree with", cxxopts::value<std::vector<size_t>>()->default_value("100000000"))
        ("repeat_lookup_measurement", "Number of times the lookup benchmark shall be repeated", cxxopts::value<std::vector<size_t>>()->default_value("10"))
        ("run_remote_memory", "Attempts to load remote memory NumaSetting from config.", cxxopts::value<std::vector<bool>>()->default_value("true,false"))
        ("run_on_node", "Which NUMA node to run the benchmark on.", cxxopts::value<std::vector<NodeID>>()->default_value("0"))
        ("alloc_on_node", "Which NUMA node to alloc memory on.", cxxopts::value<std::vector<NodeID>>()->default_value("0"))
        ("binary_search_variant", "Which binary search to use (naive,coro,state)", cxxopts::value<std::vector<std::string>>()->default_value("naive,coro,state"))
        ("key_distribution", "Kind of key distribution used for lookups (uniform, zip)", cxxopts::value<std::vector<std::string>>()->default_value("uniform,zip"))
        ("print_system_stats", "Print basic systems stats after each benchmark run.", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("reliability", "Fujitsu feature true -> weak reliability, else strong", cxxopts::value<std::vector<bool>>()->default_value("false,true"))
        ("profile", "Profile", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("zipf_theta", "Must be in (0,1) with 0 being uniform and 1 a very skewed distribution", cxxopts::value<std::vector<double>>()->default_value("0.99"))
        ("out", "filename", cxxopts::value<std::vector<std::string>>()->default_value("binary_search.json"));
    // clang-format on
    benchmark_config.parse(argc, argv);

    int benchmark_run = 0;
    std::vector<nlohmann::json> all_results;
    for (auto &runtime_config : benchmark_config.get_runtime_configs())
    {
        auto num_threads = convert<size_t>(runtime_config["num_threads"]);
        auto parallel_streams = convert<size_t>(runtime_config["parallel_streams"]);
        auto num_lookups = convert<size_t>(runtime_config["num_lookups"]);
        auto num_elements = convert<size_t>(runtime_config["num_elements"]);
        auto repeat_lookup_measurement = convert<size_t>(runtime_config["repeat_lookup_measurement"]);
        auto run_remote_memory = convert<bool>(runtime_config["run_remote_memory"]);
        auto run_on_node = convert<NodeID>(runtime_config["run_on_node"]);
        auto alloc_on_node = convert<NodeID>(runtime_config["alloc_on_node"]);
        auto binary_search_variant = convert<std::string>(runtime_config["binary_search_variant"]);
        auto key_distribution = convert<std::string>(runtime_config["key_distribution"]);
        auto reliability = convert<bool>(runtime_config["reliability"]);
        auto profile = convert<bool>(runtime_config["profile"]);
        auto zipf_theta = convert<double>(runtime_config["zipf_theta"]);

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
        BinarySearchBenchmarkConfig config =
            {
                num_threads,
                parallel_streams,
                num_lookups,
                num_elements,
                repeat_lookup_measurement,
                run_remote_memory,
                run_on_node,
                alloc_on_node,
                binary_search_variant,
                key_distribution,
                reliability,
                profile,
                zipf_theta,
            };

        if (Prefetching::get().numa_manager.node_to_available_cpus[config.run_on_node].size() < config.num_threads)
        {
            std::cout << "Cannot place " << num_threads << " threads onto node " << config.run_on_node << std::endl;
            continue;
        }

        nlohmann::json results;
        results["config"]["num_threads"] = config.num_threads;
        results["config"]["parallel_streams"] = config.parallel_streams;
        results["config"]["num_lookups"] = config.num_lookups;
        results["config"]["num_elements"] = config.num_elements;
        results["config"]["repeat_lookup_measurement"] = config.repeat_lookup_measurement;
        results["config"]["run_remote_memory"] = config.run_remote_memory;
        results["config"]["run_on_node"] = config.run_on_node;
        results["config"]["alloc_on_node"] = config.alloc_on_node;
        results["config"]["binary_search_variant"] = config.binary_search_variant;
        results["config"]["key_distribution"] = config.key_distribution;
        results["config"]["reliability"] = config.reliability;
        results["config"]["profile"] = config.profile;
        results["config"]["zipf_theta"] = config.zipf_theta;

        run_benchmark_cacheline_size(config, results);
        all_results.push_back(results);
        auto results_file = std::ofstream{convert<std::string>(runtime_config["out"])};
        nlohmann::json intermediate_json;
        intermediate_json["results"] = all_results;
        results_file << intermediate_json.dump(-1) << std::flush;
        if (convert<bool>(runtime_config["print_system_stats"]))
        {
            log_system_resources();
        }
    }

    return 0;
}