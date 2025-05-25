// Wrapper for Hash Join based on:

/**
 * @file    main.c
 * @author  Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date    Wed May 16 16:03:10 2012
 * @version $Id: main.c 3017 2012-12-07 10:56:20Z bcagri $
 *
 * @brief  Main entry point for running join implementations with given command
 * line parameters.
 *
 * (c) 2012, ETH Zurich, Systems Group
 *
 * @mainpage Main-Memory Hash Joins On Multi-Core CPUs: Tuning to the Underlying Hardware
 *
 * @section intro Introduction
 *
 * This package provides implementations of the main-memory hash join algorithms
 * described and studied in our ICDE 2013 paper. Namely, the implemented
 * algorithms are the following with the abbreviated names:
 *
 *  - NPO:    No Partitioning Join Optimized (Hardware-oblivious algo. in paper)
 *  - NPO_st: No Partitioning Join Optimized (single-threaded)
 *
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>    /* sched_setaffinity */
#include <stdio.h>    /* printf */
#include <sys/time.h> /* gettimeofday */
#include <getopt.h>   /* getopt */
#include <stdlib.h>   /* exit */
#include <string.h>   /* strcmp */
#include <limits.h>   /* INT_MAX */
#include <sys/sysinfo.h>

extern "C"
{
#include "../lib/HashJoin/no_partitioning_join.h" /* no partitioning joins: NPO, NPO_st */
#include "../lib/HashJoin/generator.h"            /* create_relation_xk */

#include "../lib/HashJoin/affinity.h" /* pthread_attr_setaffinity_np & sched_setaffinity */
#include "../lib/HashJoin/config.h"   /* autoconf header */
#include "../lib/HashJoin/types.h"    /* autoconf header */
}

#include "prefetching.hpp"
#include "../lib/utils/simple_continuous_allocator.hpp"
#include "numa/numa_memory_resource_no_jemalloc.hpp"
#include "../../config.hpp"
#include "utils/utils.hpp"
#include "utils/stats.hpp"

#include <perfcpp/event_counter.h>

#include <fstream>
#include <functional>
#include <utility>
#include <chrono>
#include <unordered_map>

//
static std::chrono::_V2::system_clock::time_point start;
static std::chrono::duration<double> build_time;
static std::chrono::duration<double> probe_time;
static perf::MultiThreadEventCounter *mt_event_counter;

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

void start_timer()
{
    start = std::chrono::high_resolution_clock::now();
}
void stop_build_timer()
{
    build_time = std::chrono::high_resolution_clock::now() - start;
}
void stop_probe_timer()
{
    probe_time = std::chrono::high_resolution_clock::now() - start;
}
TimerCalls timer_calls{start_timer, stop_build_timer, start_timer, stop_probe_timer};

void perf_start(int tid)
{
    mt_event_counter->start(tid);
}

void perf_stop(int tid)
{
    mt_event_counter->stop(tid);
}

void perf_log(void *results_ptr, char *description, size_t normalization)
{
    auto results = reinterpret_cast<nlohmann::json *>(results_ptr);
    auto str_description = std::string{description};
    Prefetching::get().perf_manager.result(*mt_event_counter, *results, str_description, normalization);
}

typedef struct algo_t algo_t;
typedef struct param_t param_t;

struct algo_t
{
    char name[128];
    int64_t (*joinAlgo)(relation_t *, relation_t *, int, size_t *, TimerCalls *, NumaAllocator *numa_alloc, PerfCalls *perf_calls, bool reliability);
};

struct param_t
{
    algo_t *algo;
    uint32_t nthreads;
    uint32_t r_size;
    uint32_t s_size;
    uint32_t r_seed;
    uint32_t s_seed;
    double skew;
    int nonunique_keys; /* non-unique keys allowed? */
    int verbose;
    int fullrange_keys; /* keys covers full int range? */
    int basic_numa;     /* alloc input chunks thread local? */
    char *perfconf;
    char *perfout;
    // added by Prefetching:
    bool run_remote_memory;
    bool profile;
    bool reliability;
};

extern char *optarg;
extern int optind, opterr, optopt;

/** An experimental feature to allocate input relations numa-local */
extern int numalocalize; /* defined in generator.c */
extern int nthreads;     /* defined in generator.c */

/** all available algorithms */
static std::unordered_map<std::string, struct algo_t> algos =
    {
        {"NPO", {"NPO", NPO}},
        {"NPO_swpf", {"NPO_swpf", NPO_swpf}},
        {"NPO_st", {"NPO_st", NPO_st}},
        {"NPO_st_swpf", {"NPO_st_swpf", NPO_st_swpf}},

};

static SimpleContinuousAllocator *cont_alloc;

void *custom_numa_alloc(size_t size, size_t alignment = 64)
{
    return cont_alloc->allocate(size, alignment);
};

void *custom_numa_free(void *ptr, size_t alignment = 64)
{
    // Don't really need to do anything here, frees are mainly used in the end as clean up.
    // So it's sufficient to just free the large regions in the end by deleting the cont_alloc.
    return nullptr;
};

NumaAllocator numa_allocator{custom_numa_alloc, custom_numa_free};

int main(int argc, char **argv)
{
    auto &benchmark_config = Prefetching::get().runtime_config;
    auto &perf_manager = Prefetching::get().perf_manager;
    perf_manager.initialize_counter_definition(get_default_perf_config_file());
    // clang-format off
    benchmark_config.add_options()
        ("repetitions", "Number of threads to use", cxxopts::value<std::vector<int>>()->default_value("5"))
        ("nthreads", "Number of threads to use", cxxopts::value<std::vector<int>>()->default_value("2"))
        ("r_size", "Number of tuples in build relation R", cxxopts::value<std::vector<int>>()->default_value("128000000"))
        ("s_size", "Number of tuples in probe relation S", cxxopts::value<std::vector<int>>()->default_value("128000000"))
        ("r_seed", "Seed value for generating relation", cxxopts::value<std::vector<int>>()->default_value("12345"))
        ("s_seed", "Seed value for generating relation S", cxxopts::value<std::vector<int>>()->default_value("54321"))
        ("skew", "Zipf skew parameter for probe relation S", cxxopts::value<std::vector<double>>()->default_value("0.0"))
        // optional flags added for completeness but should not really be considered
        ("non_unique", "Use non-unique (duplicated) keys in input relations", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("full_range", "Spread keys in relns. in full 32-bit integer range", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("basic_numa", "Numa-localize relations to threads (Experimental)", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("verbose", "Be more verbose -- show misc extra info", cxxopts::value<std::vector<bool>>()->default_value("false"))
        // end optional flags
        ("algo", "Run the hash join algorithm named (NPO, NPO_swpf, NPO_st, NPO_st_swpf)", cxxopts::value<std::vector<std::string>>()->default_value("NPO,NPO_swpf"))
        ("use_explicit_huge_pages", "Use huge pages during allocation", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("madvise_huge_pages", "Madvise kernel to create huge pages on mem regions", cxxopts::value<std::vector<bool>>()->default_value("true,false"))
        ("run_remote_memory", "Runs the benchmark on a remote memory config", cxxopts::value<std::vector<bool>>()->default_value("true,false"))
        ("profile", "Profiles the execution. Sets repeat_measurement to 1", cxxopts::value<std::vector<bool>>()->default_value("true,false"))
        ("reliability", "Fujitsu feature true -> weak reliability, else strong", cxxopts::value<std::vector<bool>>()->default_value("true,false"))
        ("out", "Path on which results should be stored", cxxopts::value<std::vector<std::string>>()->default_value("hash_join.json"));
    // clang-format on
    benchmark_config.parse(argc, argv);

    std::vector<nlohmann::json> all_results;
    for (auto &runtime_config : benchmark_config.get_runtime_configs())
    {
        auto repetitions = convert<int>(runtime_config["repetitions"]);
        auto nthreads = convert<int>(runtime_config["nthreads"]);
        auto r_size = convert<int>(runtime_config["r_size"]);
        auto s_size = convert<int>(runtime_config["s_size"]);
        auto r_seed = convert<int>(runtime_config["r_seed"]);
        auto s_seed = convert<int>(runtime_config["s_seed"]);
        auto skew = convert<double>(runtime_config["skew"]);
        auto verbose = convert<bool>(runtime_config["verbose"]);
        auto non_unique = convert<bool>(runtime_config["non_unique"]);
        auto basic_numa = convert<bool>(runtime_config["basic_numa"]);
        auto full_range = convert<bool>(runtime_config["full_range"]);
        auto run_remote_memory = convert<bool>(runtime_config["run_remote_memory"]);
        auto use_explicit_huge_pages = convert<bool>(runtime_config["use_explicit_huge_pages"]);
        auto madvise_huge_pages = convert<bool>(runtime_config["madvise_huge_pages"]);
        auto algo = convert<std::string>(runtime_config["algo"]);
        auto profile = convert<bool>(runtime_config["profile"]);
        auto reliability = convert<bool>(runtime_config["reliability"]);
        auto out = convert<std::string>(runtime_config["out"]);

        if (profile)
        {
            repetitions = 1;
        }
        nlohmann::json results;
        results["config"]["repetitions"] = repetitions;
        results["config"]["nthreads"] = nthreads;
        results["config"]["r_size"] = r_size;
        results["config"]["s_size"] = s_size;
        results["config"]["r_seed"] = r_seed;
        results["config"]["s_seed"] = s_seed;
        results["config"]["skew"] = skew;
        results["config"]["verbose"] = verbose;
        results["config"]["non_unique"] = non_unique;
        results["config"]["basic_numa"] = basic_numa;
        results["config"]["full_range"] = full_range;
        results["config"]["run_remote_memory"] = run_remote_memory;
        results["config"]["use_explicit_huge_pages"] = use_explicit_huge_pages;
        results["config"]["madvise_huge_pages"] = madvise_huge_pages;
        results["config"]["algo"] = algo;
        results["config"]["profile"] = profile;
        results["config"]["reliability"] = reliability;

        /* Command line parameters */
        param_t cmd_params;

        /* Default values if not specified on command line */
        if (!algos.contains(algo))
        {
            throw std::runtime_error("Unknown algo was given: " + algo);
        }
        cmd_params.algo = &algos.at(algo); /* NPO */
        cmd_params.nthreads = nthreads;
        /* default dataset is Workload B (described in paper) */
        cmd_params.r_size = r_size;
        cmd_params.s_size = s_size;
        cmd_params.r_seed = r_seed;
        cmd_params.s_seed = s_seed;
        cmd_params.skew = skew;
        cmd_params.verbose = verbose;
        cmd_params.perfconf = NULL;
        cmd_params.perfout = NULL;
        cmd_params.nonunique_keys = non_unique;
        cmd_params.fullrange_keys = full_range;
        cmd_params.basic_numa = basic_numa;
        cmd_params.reliability = reliability;

        if (reliability && !get_curr_hostname().starts_with("ca"))
        {
            // Ignore reliability = True on non Fujitsu nodes
            continue;
        }

        relation_t relR;
        relation_t relS;
        int64_t results_counter;

        auto run_on_node = NodeID{0};
        auto alloc_on_node = NodeID{0};
        std::vector<size_t> cpu_ids;
        if (run_remote_memory)
        {
            // TODO: Fallback to some default incase no entry is given for hostname, perhaps?
            auto remote_numa_config = get_config_entry(get_curr_hostname(), HOST_TO_REMOTE_NUMA_CONFIG);
            run_on_node = remote_numa_config.run_on;
            alloc_on_node = remote_numa_config.alloc_on;
        }

        auto numa_config = NumaConfig{run_on_node, alloc_on_node};
        if (run_remote_memory)
        {
            numa_config = get_config_entry(get_curr_hostname(), HOST_TO_REMOTE_NUMA_CONFIG);
        }
        else
        {
            numa_config = get_config_entry_default(get_curr_hostname(), HOST_TO_LOCAL_NUMA_CONFIG, {0, 0});
        }
        run_on_node = numa_config.run_on;
        alloc_on_node = numa_config.alloc_on;
        results["config"]["run_on"] = run_on_node;
        results["config"]["alloc_on"] = alloc_on_node;

        NodeID curr_id = run_on_node;
        auto &numa_manager = Prefetching::get().numa_manager;
        while (cpu_ids.size() < nthreads)
        {
            if (curr_id != run_on_node)
            {
                std::cout << "\033[1;31m[WARNING] Could not allocate enough threads on " << std::to_string(run_on_node)
                          << " also allocating on " << std::to_string(curr_id) << "\033[0m" << std::endl;
            }
            if (curr_id >= numa_manager.node_to_available_cpus.size())
            {
                throw std::runtime_error("Not enough cpus available to run " + std::to_string(nthreads) + " threads");
            }
            for (auto available_cpu : numa_manager.node_to_available_cpus[curr_id])
            {
                cpu_ids.push_back(available_cpu);
                if (cpu_ids.size() >= nthreads)
                {
                    break;
                }
            }
            curr_id++;
        }

        auto event_counter = perf_manager.get_mt_event_counter(nthreads);
        mt_event_counter = &event_counter;
        PerfCalls perf_calls{perf_start, perf_stop, perf_log, reinterpret_cast<void *>(&results), profile};

        std::cout << "=====================================================================" << std::endl;
        std::cout << "Running algo: " << algo << " madvise: " << madvise_huge_pages << " run_remote_memory: " << run_remote_memory << " profile: " << profile << std::endl;
        std::cout << "=====================================================================" << std::endl;

        /* create relation R */
        fprintf(stdout,
                "[INFO ] Creating relation R with size = %.3lf MiB, #tuples = %d : ",
                (double)sizeof(tuple_t) * cmd_params.r_size / 1024.0 / 1024.0,
                cmd_params.r_size);
        fflush(stdout);

        seed_generator(cmd_params.r_seed);

        /* to pass information to the create_relation methods */
        numalocalize = cmd_params.basic_numa;
        nthreads = cmd_params.nthreads;
        if (Prefetching::get().numa_manager.node_to_available_cpus[alloc_on_node].size() > 0)
        {
            pin_to_cpus(Prefetching::get().numa_manager.node_to_available_cpus[alloc_on_node]);
        }
        NumaMemoryResourceNoJemalloc mem_res{alloc_on_node, use_explicit_huge_pages, madvise_huge_pages};
        // We primarily want to place the hash table on the remote note, so we allocate memory for that first. This might cause relations to be pushed to other
        // nodes if not enough memory free. We dont really care too much though, as these should be prefetched by hardware prefetchers anyways.
        auto simple_continuous_allocator_hash_join = SimpleContinuousAllocator(mem_res, 3072l * (1 << 20), 512l * (1 << 20), get_curr_hostname().starts_with("ca"));
        auto simple_continuous_allocator_relations = SimpleContinuousAllocator(mem_res, 2048l * (1 << 20), 512l * (1 << 20), get_curr_hostname().starts_with("ca"));
        cont_alloc = &simple_continuous_allocator_relations;
        if (cmd_params.fullrange_keys)
        {
            create_relation_nonunique(&relR, cmd_params.r_size, INT_MAX, &numa_allocator);
        }
        else if (cmd_params.nonunique_keys)
        {
            create_relation_nonunique(&relR, cmd_params.r_size, cmd_params.r_size, &numa_allocator);
        }
        else
        {
            create_relation_pk(&relR, cmd_params.r_size, &numa_allocator);
        }
        printf("OK \n");

        /* create relation S */
        fprintf(stdout,
                "[INFO ] Creating relation S with size = %.3lf MiB, #tuples = %d : ",
                (double)sizeof(tuple_t) * cmd_params.s_size / 1024.0 / 1024.0,
                cmd_params.s_size);
        fflush(stdout);

        seed_generator(cmd_params.s_seed);

        if (cmd_params.fullrange_keys)
        {
            create_relation_fk_from_pk(&relS, &relR, cmd_params.s_size, &numa_allocator);
        }
        else if (cmd_params.nonunique_keys)
        {
            /* use size of R as the maxid */
            create_relation_nonunique(&relS, cmd_params.s_size, cmd_params.r_size, &numa_allocator);
        }
        else
        {
            /* if r_size == s_size then equal-dataset, else non-equal dataset */

            if (cmd_params.skew > 0)
            {
                /* S is skewed */
                create_relation_zipf(&relS, cmd_params.s_size,
                                     cmd_params.r_size, cmd_params.skew, &numa_allocator);
            }
            else
            {
                /* S is uniform foreign key */
                create_relation_fk(&relS, cmd_params.s_size, cmd_params.r_size, &numa_allocator);
            }
        }
        printf("OK \n");
        std::vector<double> build_times;
        std::vector<double> probe_times;
        std::vector<double> total_times;
        for (int repetition = 0; repetition < repetitions; ++repetition)
        {
            /* Run the selected join algorithm */
            printf("[INFO ] Running join algorithm %s ...\n", cmd_params.algo->name);

            // Setting new cont allocator to not cause too much leak here
            cont_alloc = &simple_continuous_allocator_hash_join;
            simple_continuous_allocator_hash_join.clear_all_allocated_regions();
            results_counter = cmd_params.algo->joinAlgo(&relR, &relS, cmd_params.nthreads, cpu_ids.data(), &timer_calls, &numa_allocator, &perf_calls, cmd_params.reliability);

            log_system_resources();
            printf("[INFO ] Results = %lu. DONE.\n", results_counter);
            std::cout << "build time:" << build_time.count() << std::endl;
            std::cout << "probe time:" << probe_time.count() << std::endl
                      << std::endl;
            build_times.push_back(build_time.count());
            probe_times.push_back(probe_time.count());
            total_times.push_back((build_time + probe_time).count());
        }
        generate_stats(results, build_times, "build_");
        generate_stats(results, probe_times, "probe_");
        generate_stats(results, total_times, "total_");
        /* clean-up */
        cont_alloc = &simple_continuous_allocator_relations;
        delete_relation(&relR, &numa_allocator);
        delete_relation(&relS, &numa_allocator);

        all_results.push_back(results);
        auto results_file = std::ofstream{convert<std::string>(runtime_config["out"])};
        nlohmann::json intermediate_json;
        intermediate_json["results"] = all_results;
        results_file << intermediate_json.dump(-1) << std::flush;
    }
    return 0;
}
