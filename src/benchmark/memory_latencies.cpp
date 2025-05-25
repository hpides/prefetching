#include "prefetching.hpp"

#include <algorithm>
#include <random>
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>
#include <numaif.h>
#include <numa.h>

#include <nlohmann/json.hpp>

#include "numa/numa_memory_resource.hpp"
#include "numa/numa_memory_resource_no_jemalloc.hpp"
#include "utils/simple_continuous_allocator.hpp"
#include "utils/utils.hpp"
#include "../../third_party/tinymembench/tinymembench.h"
#include "../../third_party/tinymembench/util.h"
#include "../../config.hpp"

struct LBenchmarkConfig
{
    int memory_size;
    int access_range;
    NodeID alloc_on_node;
    NodeID run_on_node;
    int memory_accesses;
    int repeat_measurement;
    bool use_explicit_huge_pages;
    bool madvise_huge_pages;
    bool profile;
};

// Adapted from Tinymembench
int latency_bench(LBenchmarkConfig &config, auto &results, char *buffer)
{
    double t, t2, t_before, t_after, t_noaccess, t_noaccess2;
    double xs, xs0, xs1, xs2;
    double ys, ys0, ys1, ys2;
    double min_t, min_t2;
    int nbits, n;
    pin_to_cpu(Prefetching::get().numa_manager.node_to_available_cpus[config.run_on_node][0]);

    for (n = 1; n <= config.repeat_measurement; n++)
    {
        t_before = gettime();
        random_read_test(buffer, config.memory_accesses, 1);
        t_after = gettime();
        if (n == 1 || t_after - t_before < t_noaccess)
            t_noaccess = t_after - t_before;

        t_before = gettime();
        random_dual_read_test(buffer, config.memory_accesses, 1);
        t_after = gettime();
        if (n == 1 || t_after - t_before < t_noaccess2)
            t_noaccess2 = t_after - t_before;
    }

    printf("\nblock size : single random read / dual random read");
    if (!config.madvise_huge_pages && !config.use_explicit_huge_pages)
        printf(", [NOHUGEPAGE]\n");
    else if (config.madvise_huge_pages)
        printf(", [MADV_HUGEPAGE]\n");
    else if (config.use_explicit_huge_pages)
        printf(", [MMAP_HUGEPAGE]\n");
    else
        throw std::logic_error("hugepage config wrong");

    int testsize = config.access_range;
    xs1 = xs2 = ys = ys1 = ys2 = 0;
    auto &perf_manager = Prefetching::get().perf_manager;
    auto event_counter = perf_manager.get_event_counter();
    for (n = 1; n <= config.repeat_measurement; n++)
    {
        /*
         * Select a random offset in order to mitigate the unpredictability
         * of cache associativity effects when dealing with different
         * physical memory fragmentation (for PIPT caches). We are reporting
         * the "best" measured latency, some offsets may be better than
         * the others.
         */
        int testoffs = (rand32() % ((config.memory_size << 20) / testsize)) * testsize;

        if (config.profile)
        {
            event_counter.start();
        }
        t_before = gettime();
        random_read_test(buffer + testoffs, config.memory_accesses, testsize);
        t_after = gettime();

        if (config.profile)
        {
            event_counter.stop();
            perf_manager.result(event_counter, results, config.memory_accesses);
        }
        t = t_after - t_before - t_noaccess;
        if (t < 0)
            t = 0;

        xs1 += t;
        xs2 += t * t;

        if (n == 1 || t < min_t)
            min_t = t;

        t_before = gettime();
        random_dual_read_test(buffer + testoffs, config.memory_accesses, testsize);
        t_after = gettime();
        t2 = t_after - t_before - t_noaccess2;
        if (t2 < 0)
            t2 = 0;

        ys1 += t2;
        ys2 += t2 * t2;

        if (n == 1 || t2 < min_t2)
            min_t2 = t2;

        if (n > 2)
        {
            xs = sqrt((xs2 * n - xs1 * xs1) / (n * (n - 1)));
            ys = sqrt((ys2 * n - ys1 * ys1) / (n * (n - 1)));
            if (xs < min_t / 1000. && ys < min_t2 / 1000.)
                break;
        }
    }
    printf("%10d : %6.1f ns          /  %6.1f ns \n", config.access_range,
           min_t * 1000000000. / config.memory_accesses, min_t2 * 1000000000. / config.memory_accesses);

    results["latency_single"] = min_t * 1000000000. / config.memory_accesses;
    results["latency_double"] = min_t2 * 1000000000. / config.memory_accesses;
    return 1;
}

size_t resolve(size_t *buffer, size_t resolves, size_t start)
{
    size_t count = 0;
    size_t curr = start;
    while (count < resolves)
    {
        curr = buffer[curr];
        count++;
    }
    return curr;
}

int pointer_chase(LBenchmarkConfig &config, auto &results, size_t *buffer, auto &allocator)
{
    pin_to_cpu(Prefetching::get().numa_manager.node_to_available_cpus[config.run_on_node][0]);

    initialize_pointer_chase(buffer, config.access_range / sizeof(size_t));

    size_t *zero_buffer = reinterpret_cast<size_t *>(allocator.allocate(sizeof(size_t), alignof(size_t)));
    zero_buffer[0] = 0;

    std::cout << std::endl
              << "block size : single random read";
    if (!config.madvise_huge_pages && !config.use_explicit_huge_pages)
        std::cout << ", [NOHUGEPAGE]\n";
    else if (config.madvise_huge_pages)
        std::cout << ", [MADV_HUGEPAGE]\n";
    else if (config.use_explicit_huge_pages)
        std::cout << ", [MMAP_HUGEPAGE]\n";
    else
        throw std::logic_error("hugepage config wrong");
    std::vector<std::chrono::duration<double>> baseline_durations(config.repeat_measurement);
    std::vector<std::chrono::duration<double>> access_durations(config.repeat_measurement);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, (config.access_range / sizeof(size_t)) - 1);
    auto &perf_manager = Prefetching::get().perf_manager;
    auto event_counter = perf_manager.get_event_counter();
    for (int n = 0; n < config.repeat_measurement; n++)
    {
        auto start = std::chrono::high_resolution_clock::now();
        auto r = resolve(zero_buffer, config.memory_accesses, 0);
        auto end = std::chrono::high_resolution_clock::now();

        baseline_durations[n] = end - start;

        if (config.profile)
        {
            event_counter.start();
        }
        start = std::chrono::high_resolution_clock::now();
        r += resolve(buffer, config.memory_accesses, dis(gen));
        end = std::chrono::high_resolution_clock::now();

        if (config.profile)
        {
            event_counter.stop();
            perf_manager.result(event_counter, results, config.memory_accesses);
        }

        access_durations[n] = end - start;

        if (r > config.access_range / sizeof(size_t))
        {
            throw std::runtime_error("error occurred during resolve. " + std::to_string(r) + " returned.");
        }
    }
    std::cout << "Baseline_Durations:";
    for (auto &no_d : baseline_durations)
    {
        std::cout << " " << no_d.count();
    }
    std::cout << std::endl;
    std::cout << "Access_durations:";
    for (auto &d : access_durations)
    {
        std::cout << " " << d.count();
    }
    std::cout << std::endl;
    std::vector<double> latencies_ns;
    latencies_ns.reserve(access_durations.size());
    for (unsigned i = 0; i < access_durations.size(); i++)
    {
        latencies_ns.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(access_durations.at(i) - baseline_durations.at(i)).count() / static_cast<double>(config.memory_accesses));
    }
    results["min_latency_single"] = *std::min_element(latencies_ns.begin(), latencies_ns.end());
    results["median_latency_single"] = findMedian(latencies_ns, latencies_ns.size());
    results["latency_single"] = results["median_latency_single"];
    results["latencies"] = latencies_ns;

    std::cout << config.access_range << " : " << results["latency_single"] << std::endl;

    return 1;
}

int main(int argc, char **argv)
{
    auto &benchmark_config = Prefetching::get().runtime_config;
    auto &perf_manager = Prefetching::get().perf_manager;
    perf_manager.initialize_counter_definition(get_default_perf_config_file());
    // clang-format off
    benchmark_config.add_options()
        ("memory_size", "Total memory allocated MiB", cxxopts::value<std::vector<int>>()->default_value("1024"))
        ("start_access_range", "start memory accesses range (in Bytes, max ~1GiB)", cxxopts::value<std::vector<int>>()->default_value("1024"))
        ("end_access_range", "end memory accesses range (in Bytes, max ~1GiB)", cxxopts::value<std::vector<int>>()->default_value("536870912"))
        ("growth_factor", "Factor with which the access range grows per iteration", cxxopts::value<std::vector<double>>()->default_value("1.1"))
        ("alloc_on_node", "Defines on which NUMA node the benchmark allocates memory", cxxopts::value<std::vector<NodeID>>()->default_value("0"))
        ("run_on_node", "Defines on which NUMA node the benchmark is run", cxxopts::value<std::vector<NodeID>>()->default_value("0"))
        ("memory_accesses", "Number of memory accesses per configuration", cxxopts::value<std::vector<int>>()->default_value("10000000"))
        ("repeat_measurement", "Number of repeats of the benchmark lookup phase", cxxopts::value<std::vector<int>>()->default_value("10"))
        ("use_pointer_chase", "Use pointer chase variant of benchmark", cxxopts::value<std::vector<bool>>()->default_value("true"))
        ("use_explicit_huge_pages", "Use huge pages during allocation", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("madvise_huge_pages", "Madvise kernel to create huge pages on mem regions", cxxopts::value<std::vector<bool>>()->default_value("true"))
        ("generate_numa_matrix", "Automatically iterates over all possible alloc and run configurations", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("profile", "Profiles the execution. Sets repeat_measurement to 1.", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("out", "Path on which results should be stored", cxxopts::value<std::vector<std::string>>()->default_value("latency_benchmark.json"));
    // clang-format on
    benchmark_config.parse(argc, argv);

    int benchmark_run = 0;

    std::vector<nlohmann::json> all_results;
    for (auto &runtime_config : benchmark_config.get_runtime_configs())
    {
        auto memory_size = convert<int>(runtime_config["memory_size"]);
        auto start_access_range = convert<int>(runtime_config["start_access_range"]);
        auto end_access_range = convert<int>(runtime_config["end_access_range"]);
        auto growth_factor = convert<double>(runtime_config["growth_factor"]);
        auto alloc_on_node = convert<NodeID>(runtime_config["alloc_on_node"]);
        auto run_on_node = convert<NodeID>(runtime_config["run_on_node"]);
        auto memory_accesses = convert<int>(runtime_config["memory_accesses"]);
        auto repeat_measurement = convert<int>(runtime_config["repeat_measurement"]);
        auto use_explicit_huge_pages = convert<bool>(runtime_config["use_explicit_huge_pages"]);
        auto madvise_huge_pages = convert<bool>(runtime_config["madvise_huge_pages"]);
        auto generate_numa_matrix = convert<bool>(runtime_config["generate_numa_matrix"]);
        auto out = convert<std::string>(runtime_config["out"]);
        auto use_pointer_chase = convert<bool>(runtime_config["use_pointer_chase"]);
        auto profile = convert<bool>(runtime_config["profile"]);

        if (profile)
        {
            repeat_measurement = 1;
        }

        LBenchmarkConfig config = {
            memory_size,
            start_access_range,
            alloc_on_node,
            run_on_node,
            memory_accesses,
            repeat_measurement,
            use_explicit_huge_pages,
            madvise_huge_pages,
            profile,
        };

        std::vector<NodeID> alloc_on_nodes = {NodeID{alloc_on_node}};
        std::vector<NodeID> run_on_nodes = {NodeID{run_on_node}};

        const auto &nm = Prefetching::get().numa_manager;
        if (generate_numa_matrix)
        {
            alloc_on_nodes = nm.mem_nodes;
            run_on_nodes = nm.active_nodes;
        }

        for (NodeID alloc_on : alloc_on_nodes)
        {
            config.alloc_on_node = alloc_on;
            if (Prefetching::get().numa_manager.node_to_available_cpus[config.alloc_on_node].size() > 0)
            {
                pin_to_cpus(Prefetching::get().numa_manager.node_to_available_cpus[config.alloc_on_node]);
            }
            auto mem_res = NumaMemoryResourceNoJemalloc{config.alloc_on_node, config.use_explicit_huge_pages, config.madvise_huge_pages};
            auto simple_continuous_allocator = SimpleContinuousAllocator{mem_res, 1024l * (1 << 20), 512l * (1 << 20)};

            auto buffer_size = (use_pointer_chase) ? end_access_range : (config.memory_size << 20);
            auto buffer = simple_continuous_allocator.allocate(buffer_size);
            memset(buffer, 1, buffer_size);

            for (NodeID run_on : run_on_nodes)
            {
                config.run_on_node = run_on;

                std::cout << "alloc_on_node: " << config.alloc_on_node << " run_on_node: " << config.run_on_node << std::endl;

                for (int access_range = start_access_range; access_range <= end_access_range; access_range *= growth_factor)
                {
                    config.access_range = access_range;
                    nlohmann::json results;
                    results["config"]["memory_size"] = config.memory_size;
                    results["config"]["access_range"] = access_range;
                    results["config"]["alloc_on_node"] = config.alloc_on_node;
                    results["config"]["repeat_measurement"] = config.repeat_measurement;
                    results["config"]["memory_accesses"] = config.memory_accesses;
                    results["config"]["run_on_node"] = config.run_on_node;
                    results["config"]["use_explicit_huge_pages"] = config.use_explicit_huge_pages;
                    results["config"]["madvise_huge_pages"] = config.madvise_huge_pages;
                    results["config"]["use_pointer_chase"] = use_pointer_chase;
                    results["config"]["profile"] = config.profile;
                    if (use_pointer_chase)
                    {
                        pointer_chase(config, results, reinterpret_cast<size_t *>(buffer), simple_continuous_allocator);
                    }
                    else
                    {
                        latency_bench(config, results, reinterpret_cast<char *>(buffer));
                    }
                    all_results.push_back(results);
                }
            }
            simple_continuous_allocator.deallocate(buffer, buffer_size);
        }

        auto results_file = std::ofstream{out};
        nlohmann::json intermediate_json;
        intermediate_json["results"] = all_results;
        results_file << intermediate_json.dump(-1) << std::flush;
    }

    return 0;
}
