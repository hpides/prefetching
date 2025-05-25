#include "prefetching.hpp"

#include <random>
#include <chrono>
#include <thread>
#include <iostream>
#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

#include "numa/numa_memory_resource.hpp"
#include "numa/static_numa_memory_resource.hpp"
#include "utils/simple_continuous_allocator.hpp"
#include "utils/utils.cpp"
#include "utils/stats.hpp"
#include "../../config.hpp"

const size_t CACHELINE_SIZE = get_cache_line_size();
const size_t REPEATS = 10;

struct LFBReliabilityBenchmarkConfig
{
    size_t total_memory;
    size_t num_threads;
    size_t num_repetitions;
    size_t num_resolves;
    std::string locality_hint;
    bool data_dependency;
    bool use_explicit_huge_pages;
    bool madvise_huge_pages;
    bool profile;
    bool reliability;
};
const uintptr_t ca_weak_reliability_mask = uintptr_t(1) << 60;

template <int locality, bool data_dependency>
void reliability(
    size_t i, size_t number_accesses,
    const LFBReliabilityBenchmarkConfig &config,
    auto &data, auto &accesses, auto &prefetch_durations,
    auto &access_durations, auto &mt_event_counter)
{
    pin_to_cpu(Prefetching::get().numa_manager.node_to_available_cpus[0][i]);
    size_t start_access = i * number_accesses;

    int dummy_dependency = 0; // data has only zeros written to it, so this will effectively do nothing, besides
                              // adding a data dependency - Hopefully this forces batches to be loaded sequentially.

    std::chrono::duration<double> prefetch_duration{0};
    std::chrono::duration<double> access_duration{0};
    if (config.profile)
    {
        mt_event_counter.start(i);
    }
    size_t b = 0;
    for (; b + config.num_resolves < number_accesses; b += config.num_resolves)
    {
        auto start = std::chrono::high_resolution_clock::now();
        if (config.reliability)
        {
            for (int j = 0; j < config.num_resolves; ++j)
            {
                __builtin_prefetch(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(data.data() + accesses[start_access + b + j + dummy_dependency]) | ca_weak_reliability_mask), 0, locality);
            }
        }
        else
        {
            for (int j = 0; j < config.num_resolves; ++j)
            {
                __builtin_prefetch(reinterpret_cast<void *>(data.data() + accesses[start_access + b + j + dummy_dependency]), 0, locality);
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        prefetch_duration += end - start;

        int new_dummy = 0;
        start = std::chrono::high_resolution_clock::now();
        for (int j = 0; j < config.num_resolves; ++j)
        {
            if constexpr (data_dependency)
            {
                new_dummy += data[accesses[start_access + b + j + new_dummy]];
            }
            else
            {
                new_dummy += data[accesses[start_access + b + j]];
            }
        }
        end = std::chrono::high_resolution_clock::now();
        access_duration += end - start;
        dummy_dependency += new_dummy;
    }
    if (config.profile)
    {
        mt_event_counter.stop(i);
    }

    if (dummy_dependency != 0)
    {
        throw std::runtime_error("Dependency != 0. (Got : " + std::to_string(dummy_dependency) + " instead)");
    }
    prefetch_durations[i] = prefetch_duration / b;
    access_durations[i] = access_duration / b;
};

template <int locality, bool data_dependency>
void benchmark_wrapper(LFBReliabilityBenchmarkConfig &config, nlohmann::json &results, auto &zero_data)
{

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, zero_data.size() - 1);

    std::vector<std::uint64_t> accesses(config.num_repetitions);
    size_t repeats = (config.profile) ? 1 : REPEATS;
    std::vector<double> measurement_prefetch_durations(repeats);
    std::vector<double> measurement_access_durations(repeats);

    auto mt_event_counter = Prefetching::get().perf_manager.get_mt_event_counter(config.num_threads);
    for (size_t r = 0; r < repeats; r++)
    {
        size_t number_accesses_per_thread = config.num_repetitions / config.num_threads;
        std::vector<std::jthread> threads;
        std::vector<std::chrono::duration<double>> prefetch_durations(config.num_threads);
        std::vector<std::chrono::duration<double>> access_durations(config.num_threads);

        std::generate(accesses.begin(), accesses.end(), [&]()
                      { return dis(gen); });

        for (size_t i = 0; i < config.num_threads; ++i)
        {
            threads.emplace_back([&, i]()
                                 { reliability<locality, data_dependency>(
                                       i, number_accesses_per_thread, config,
                                       zero_data, accesses, prefetch_durations,
                                       access_durations, mt_event_counter); });
        }
        for (auto &t : threads)
        {
            t.join();
        }
        threads.clear();
        auto total_prefetch_time = std::chrono::duration<double>{0};
        auto total_access_time = std::chrono::duration<double>{0};
        for (auto &duration : prefetch_durations)
        {
            total_prefetch_time += duration;
        }
        for (auto &duration : access_durations)
        {
            total_access_time += duration;
        }
        measurement_prefetch_durations[r] = (total_prefetch_time / config.num_threads).count();
        measurement_access_durations[r] = (total_access_time / config.num_threads).count();
    }
    generate_stats(results, measurement_prefetch_durations, "prefetch_");
    generate_stats(results, measurement_access_durations, "access_");
    if (config.profile)
    {
        Prefetching::get().perf_manager.result(mt_event_counter, results, config.num_repetitions);
    }
    std::cout << "num_resolves: " << config.num_resolves << " locality: " << config.locality_hint << " data_depedency:" << data_dependency << std::endl;
    std::cout << "took " << results["prefetch_runtime"] << " + " << results["access_runtime"] << std::endl;
}

template <int locality>
std::function<void(LFBReliabilityBenchmarkConfig &, nlohmann::json &, std::pmr::vector<char> &)>
benchmark_unpack_data_dependency(LFBReliabilityBenchmarkConfig &config)
{
    if (config.data_dependency)
    {
        return [](LFBReliabilityBenchmarkConfig &config, nlohmann::json &results, std::pmr::vector<char> &data)
        {
            benchmark_wrapper<locality, true>(config, results, data);
        };
    }
    else
    {
        return [](LFBReliabilityBenchmarkConfig &config, nlohmann::json &results, std::pmr::vector<char> &data)
        {
            benchmark_wrapper<locality, false>(config, results, data);
        };
    }
}

std::function<void(LFBReliabilityBenchmarkConfig &, nlohmann::json &, std::pmr::vector<char> &)>
benchmark_unpack_config(LFBReliabilityBenchmarkConfig &config)
{
    if (config.locality_hint == "NTA")
    {
        // _MM_HINT_NTA
        return benchmark_unpack_data_dependency<0>(config);
    }
    else if (config.locality_hint == "T0")
    {
        // _MM_HINT_T0
        return benchmark_unpack_data_dependency<3>(config);
    }
    else if (config.locality_hint == "T1")
    {
        // _MM_HINT_T1
        return benchmark_unpack_data_dependency<2>(config);
    }
    else if (config.locality_hint == "T2")
    {
        // _MM_HINT_T2
        return benchmark_unpack_data_dependency<1>(config);
    }
    else
    {
        throw std::runtime_error("Unknown locality_hint given: " + config.locality_hint);
    }
}

int main(int argc, char **argv)
{
    auto &benchmark_config = Prefetching::get().runtime_config;
    Prefetching::get().perf_manager.initialize_counter_definition(get_default_perf_config_file());
    // clang-format off
    benchmark_config.add_options()
        ("total_memory", "Total memory allocated MiB", cxxopts::value<std::vector<size_t>>()->default_value("512"))
        ("num_threads", "Number of threads running the bench", cxxopts::value<std::vector<size_t>>()->default_value("1"))
        ("num_repetitions", "Number of repetitions of the measurement", cxxopts::value<std::vector<size_t>>()->default_value("10000000"))
        ("start_num_resolves", "starting number of resolves per iteration", cxxopts::value<std::vector<size_t>>()->default_value("1"))
        ("end_num_resolves", "ending number of resolves per iteration", cxxopts::value<std::vector<size_t>>()->default_value("64"))
        ("locality_hint", "locality_hint, can be nta, t0, t1, or t2", cxxopts::value<std::vector<std::string>>()->default_value("NTA,T0,T1,T2"))
        ("data_dependency", "Add a data dependency during accessing.", cxxopts::value<std::vector<bool>>()->default_value("true"))
        ("use_explicit_huge_pages", "Use huge pages during allocation", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("madvise_huge_pages", "Madvise kernel to create huge pages on mem regions", cxxopts::value<std::vector<bool>>()->default_value("true"))
        ("profile", "Turn perf stats on.", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("reliability", "Tag address -> true means weak reliability (only works on Fujitsu A64FX).", cxxopts::value<std::vector<bool>>()->default_value("false"))
        ("out", "Path on which results should be stored", cxxopts::value<std::vector<std::string>>()->default_value("lfb_reliability.json"));
    // clang-format on
    benchmark_config.parse(argc, argv);

    int benchmark_run = 0;

    std::vector<nlohmann::json> all_results;
    for (auto &runtime_config : benchmark_config.get_runtime_configs())
    {
        auto total_memory = convert<size_t>(runtime_config["total_memory"]);
        auto num_threads = convert<size_t>(runtime_config["num_threads"]);
        auto num_repetitions = convert<size_t>(runtime_config["num_repetitions"]);
        auto start_num_resolves = convert<size_t>(runtime_config["start_num_resolves"]);
        auto end_num_resolves = convert<size_t>(runtime_config["end_num_resolves"]);
        auto locality_hint = convert<std::string>(runtime_config["locality_hint"]);
        auto data_dependency = convert<bool>(runtime_config["data_dependency"]);
        auto use_explicit_huge_pages = convert<bool>(runtime_config["use_explicit_huge_pages"]);
        auto madvise_huge_pages = convert<bool>(runtime_config["madvise_huge_pages"]);
        auto profile = convert<bool>(runtime_config["profile"]);
        auto reliability = convert<bool>(runtime_config["reliability"]);
        auto out = convert<std::string>(runtime_config["out"]);

        LFBReliabilityBenchmarkConfig config = {
            total_memory,
            num_threads,
            num_repetitions,
            0,
            locality_hint,
            data_dependency,
            use_explicit_huge_pages,
            madvise_huge_pages,
            profile,
            reliability,
        };
        pin_to_cpu(Prefetching::get().numa_manager.node_to_available_cpus[0].back());
        auto total_memory_bytes = config.total_memory * 1024 * 1024; // memory given in MiB
        StaticNumaMemoryResource mem_res{0, config.use_explicit_huge_pages, config.madvise_huge_pages};
        auto simple_continuous_allocator = SimpleContinuousAllocator(mem_res, 2096l * (1 << 20), 512l * (1 << 20));

        std::pmr::vector<char> data(total_memory_bytes, &simple_continuous_allocator);

        memset(data.data(), total_memory_bytes, 0);
        for (size_t num_resolves = start_num_resolves; num_resolves <= end_num_resolves; num_resolves++)
        {
            config.num_resolves = num_resolves;

            nlohmann::json results;
            results["config"]["total_memory"] = config.total_memory;
            results["config"]["num_threads"] = config.num_threads;
            results["config"]["num_repetitions"] = config.num_repetitions;
            results["config"]["num_resolves"] = num_resolves;
            results["config"]["locality_hint"] = config.locality_hint;
            results["config"]["data_dependency"] = config.data_dependency;
            results["config"]["use_explicit_huge_pages"] = config.use_explicit_huge_pages;
            results["config"]["madvise_huge_pages"] = config.madvise_huge_pages;
            results["config"]["reliability"] = config.reliability;
            results["config"]["profile"] = config.profile;
            benchmark_unpack_config(config)(config, results, data);
            all_results.push_back(results);
        }
        auto results_file = std::ofstream{out};
        nlohmann::json intermediate_json;
        intermediate_json["results"] = all_results;
        results_file << intermediate_json.dump(-1) << std::flush;
    }

    return 0;
}