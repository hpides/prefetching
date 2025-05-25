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
const size_t REPEATS = 10;

enum GrowthStrategy
{
    FillNumaNodesFirst, // First use all cpus on local node, than next node, and so on
    BalanceOnNumaNodes, // Place threads in a round robin fashion among all numa nodes
};

struct LFBBenchmarkConfig
{
    size_t total_memory;
    size_t num_threads;
    size_t num_total_accesses;
    GrowthStrategy growth_strategy;
    bool use_smt;
    bool use_explicit_huge_pages;
    bool madvise_huge_pages;
};

void generate_stats(auto &results, auto &measurements, std::string name)
{
    results["min_" + name] = *std::min_element(measurements.begin(), measurements.end());
    results["median_" + name] = findMedian(measurements, measurements.size());
    results[name] = results["median_" + name];
    results[name + "_measurements"] = measurements;
}

void batched_load_simplified(size_t i, std::atomic_int64_t &running_threads, size_t number_accesses, LFBBenchmarkConfig &config, auto &data, auto &accesses, auto &durations, NodeID run_on_cpu)
{
    pin_to_cpu(run_on_cpu);
    size_t start_access = i * number_accesses;
    volatile int dummy_dependency = 0; // data has only zeros written to it, so this will effectively do nothing, besides
                                       // adding a data dependency - Hopefully this forces batches to be loaded sequentially.

    const auto data_size = data.size();

    running_threads++;
    while (running_threads != config.num_threads)
    {
        // wait for other threads to reach this point.
    };

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t j = 0; j < number_accesses; ++j)
    {
        auto offset = start_access; // thread offset
        const auto random_access = accesses[offset + j];
        dummy_dependency = dummy_dependency + *reinterpret_cast<uint8_t *>(data.data() + random_access);
    }
    if (dummy_dependency > data_size)
    {
        throw std::runtime_error("new_dep contains wrong dependency: " + std::to_string(dummy_dependency));
    }
    auto end = std::chrono::high_resolution_clock::now();
    durations[i] = std::chrono::duration<double>(end - start);
};

void lfb_size_benchmark(LFBBenchmarkConfig config, nlohmann::json &results, auto &zero_data, std::vector<NodeID> cpu_ids)
{

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, zero_data.size() - 1);

    std::vector<std::uint64_t> accesses(config.num_total_accesses);

    const size_t read_size = 8;
    const size_t number_accesses_per_thread = config.num_total_accesses / config.num_threads;
    // fill accesses with random numbers from 0 to total_memory (in bytes) - read_size.
    std::generate(accesses.begin(), accesses.end(), [&]()
                  { return dis(gen); });

    auto min_time = std::chrono::duration<double>{std::numeric_limits<double>::max()}.count();
    std::vector<double> measurement_durations(REPEATS);
    std::vector<double> duration_divided_by_threads(REPEATS);
    std::vector<double> accesses_per_second(REPEATS);

    for (int i = 0; i < REPEATS; ++i)
    {
        std::vector<std::jthread> threads;

        std::vector<std::chrono::duration<double>> durations(config.num_threads);
        auto running_threads = std::atomic_int64_t{0};
        auto curr_numa_node = NodeID{0};
        auto curr_cpu = Prefetching::get().numa_manager.node_to_available_cpus[curr_numa_node][0];
        for (size_t i = 0; i < config.num_threads; ++i)
        {

            threads.emplace_back([&, i]()
                                 { batched_load_simplified(i, running_threads, number_accesses_per_thread, config, zero_data, accesses, durations, cpu_ids[i]); });
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
        measurement_durations[i] = total_time.count();
        duration_divided_by_threads[i] = total_time.count() / config.num_threads;
        accesses_per_second[i] = config.num_total_accesses / total_time.count() / config.num_threads;
    }
    generate_stats(results, measurement_durations, "runtime_sum_all_threads");
    generate_stats(results, duration_divided_by_threads, "runtime");
    generate_stats(results, measurement_durations, "access_per_second");

    std::cout << "growth_strategy: " << ((config.growth_strategy == BalanceOnNumaNodes) ? "BalanceOnNumaNodes" : "FillNumaNodesFirst") << std::endl;
    std::cout << "smt: " << std::to_string(config.use_smt) << std::endl;
    std::cout << "num_threads: " << config.num_threads << std::endl;
    std::cout << "running on: ";
    for (auto node : cpu_ids)
    {
        std::cout << std::to_string(node) << " ";
    }
    std::cout << std::endl;
    std::cout << "took " << results["runtime"] << " seconds " << std::endl;
}

struct pair_hash
{
    template <class T1, class T2>
    std::size_t operator()(const std::pair<T1, T2> &pair) const
    {
        return std::hash<T1>()(pair.first) ^ std::hash<T2>()(pair.second);
    }
};

std::vector<std::vector<NodeID>> get_cpu_ids(GrowthStrategy growth_strategy, bool use_smt)
{
    std::vector<std::vector<NodeID>> cpu_ids;
    auto &numa_manager = Prefetching::get().numa_manager;
    auto &active_nodes = numa_manager.active_nodes;
    std::vector<NodeID> curr_cpus;
    std::unordered_set<std::pair<NodeID, NodeID>, pair_hash> used_cores; // stores Numa-node and allocated cpus on that node.
    std::vector<std::tuple<std::vector<NodeID>::iterator, std::vector<NodeID>::iterator, NodeID>> node_iterators;
    switch (growth_strategy)
    {
    case BalanceOnNumaNodes:
    {

        // not really perfectly based in some cases, but should be good enough.
        for (NodeID numa_node : active_nodes)
        {
            node_iterators.emplace_back(numa_manager.node_to_available_cpus[numa_node].begin(), numa_manager.node_to_available_cpus[numa_node].end(), numa_node);
        }
        bool added = true;
        while (added)
        {
            added = false;
            for (auto &[it, end, numa_node] : node_iterators)
            {
                if (it != end)
                {
                    if (!use_smt)
                    {
                        if (used_cores.contains({numa_node, numa_manager.cpu_to_core_id[*it]}))
                        {
                            ++it;
                            continue;
                        }
                    }
                    added = true;
                    used_cores.insert({numa_node, numa_manager.cpu_to_core_id[*it]});
                    curr_cpus.push_back(*it);
                    cpu_ids.emplace_back(curr_cpus);
                    ++it;
                }
            }
        }
        break;
    }

    case FillNumaNodesFirst:
    {

        // start on numa node 0, and add cpus from that node

        for (NodeID numa_node : active_nodes)
        {
            for (NodeID cpu : numa_manager.node_to_available_cpus[numa_node])
            {
                if (!use_smt)
                {
                    if (used_cores.contains({numa_node, numa_manager.cpu_to_core_id[cpu]}))
                    {
                        continue;
                    }
                }
                curr_cpus.push_back(cpu);
                cpu_ids.emplace_back(curr_cpus);
                used_cores.insert({numa_node, numa_manager.cpu_to_core_id[cpu]});
            }
        }
        break;
    }

    default:
        throw std::runtime_error("Unknown growth_strategy encountered: " + std::to_string(growth_strategy));
    }
    return cpu_ids;
}
int main(int argc, char **argv)
{
    auto &benchmark_config = Prefetching::get().runtime_config;

    // clang-format off
    benchmark_config.add_options()
        ("total_memory", "Total memory allocated MiB", cxxopts::value<std::vector<size_t>>()->default_value("2048"))
        ("num_total_accesses", "Number of total measured memory accesses", cxxopts::value<std::vector<size_t>>()->default_value("100000000"))
        ("growth_strategy", "Strategy used for growing the number of threads", cxxopts::value<std::vector<std::string>>()->default_value("FillNumaNodesFirst,BalanceOnNumaNodes"))
        ("use_smt", "Disable/Enable multi Threading", cxxopts::value<std::vector<bool>>()->default_value("true,false"))
        ("madvise_huge_pages", "Madvise kernel to create huge pages on mem regions", cxxopts::value<std::vector<bool>>()->default_value("true"))
        ("use_explicit_huge_pages", "Use huge pages during allocation", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("out", "Path on which results should be stored", cxxopts::value<std::vector<std::string>>()->default_value("lfb_scale_benchmark.json"));
    // clang-format on
    benchmark_config.parse(argc, argv);

    int benchmark_run = 0;

    std::vector<nlohmann::json> all_results;
    for (auto &runtime_config : benchmark_config.get_runtime_configs())
    {
        auto total_memory = convert<size_t>(runtime_config["total_memory"]);
        auto num_total_accesses = convert<size_t>(runtime_config["num_total_accesses"]);
        auto growth_strategy_string = convert<std::string>(runtime_config["growth_strategy"]);
        auto use_smt = convert<bool>(runtime_config["use_smt"]);
        auto madvise_huge_pages = convert<bool>(runtime_config["madvise_huge_pages"]);
        auto use_explicit_huge_pages = convert<bool>(runtime_config["use_explicit_huge_pages"]);
        auto out = convert<std::string>(runtime_config["out"]);

        GrowthStrategy growth_strategy;
        if (growth_strategy_string == "FillNumaNodesFirst")
        {
            growth_strategy = FillNumaNodesFirst;
        }
        else if (growth_strategy_string == "BalanceOnNumaNodes")
        {
            growth_strategy = BalanceOnNumaNodes;
        }
        else
        {
            throw std::runtime_error("Unknown growth_strategy encountered: " + growth_strategy);
        }

        LFBBenchmarkConfig config = {
            total_memory,
            0,
            num_total_accesses,
            growth_strategy,
            use_smt,
            use_explicit_huge_pages,
            madvise_huge_pages,
        };

        auto total_memory_bytes = config.total_memory * 1024 * 1024; // memory given in MiB
        StaticNumaMemoryResource mem_res{0, config.use_explicit_huge_pages, config.madvise_huge_pages};

        std::pmr::vector<char> data(total_memory_bytes, &mem_res);

        memset(data.data(), total_memory_bytes, 0);
        // sleep(total_memory / 1.2);
        for (auto &cpu_ids : get_cpu_ids(config.growth_strategy, config.use_smt))
        {
            config.num_threads = cpu_ids.size();
            nlohmann::json results;
            results["config"]["total_memory"] = config.total_memory;
            results["config"]["num_threads"] = config.num_threads;
            results["config"]["num_total_accesses"] = config.num_total_accesses;
            results["config"]["use_smt"] = config.use_smt;
            results["config"]["use_explicit_huge_pages"] = config.use_explicit_huge_pages;
            results["config"]["madvise_huge_pages"] = config.madvise_huge_pages;
            results["config"]["growth_strategy"] = (config.growth_strategy == FillNumaNodesFirst) ? "fill_numa_nodes_first" : "balanced";
            lfb_size_benchmark(config, results, data, cpu_ids);
            all_results.push_back(results);
        }
        auto results_file = std::ofstream{out};
        nlohmann::json intermediate_json;
        intermediate_json["results"] = all_results;
        results_file << intermediate_json.dump(-1) << std::flush;
    }

    return 0;
}