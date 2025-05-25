#include "hashmap.hpp"
#include "prefetching.hpp"

#include <random>
#include <functional>
#include <chrono>
#include <assert.h>
#include <nlohmann/json.hpp>
#include <fstream>

#include "zipfian_int_distribution.hpp"
#include "numa/static_numa_memory_resource.hpp"

const int TOTAL_QUERIES = 25'000'000;
const int GROUP_SIZE = 32;
const int AMAC_REQUESTS_SIZE = 1024;

template <typename Function>
void measure_vectorized_operation(HashMap<uint32_t, uint32_t> &openMap, Function func, const std::string &op_name, int invoke_vector_size, auto gen, auto dis, nlohmann::json &metrics)
{
    openMap.profiler.reset();
    auto start = std::chrono::high_resolution_clock::now();
    auto end = std::chrono::high_resolution_clock::now();
    double total_time = 0;

    std::vector<uint32_t> requests(invoke_vector_size);
    std::vector<uint32_t> results(invoke_vector_size);
    for (int i = 0; i < TOTAL_QUERIES; i += invoke_vector_size)
    {
        for (int j = 0; j < invoke_vector_size; j++)
        {
            int random_number = dis(gen); // will generate duplicates, we don't care
            requests.at(j) = random_number;
        }

        start = std::chrono::high_resolution_clock::now();
        func(requests, results, GROUP_SIZE);
        end = std::chrono::high_resolution_clock::now();
        total_time += std::chrono::duration<double>(end - start).count();

        for (int j = 0; j < invoke_vector_size; j++)
        {
            assert(results.at(j) == requests.at(j) + 1);
        }
    }

    double throughput = TOTAL_QUERIES / total_time;

    std::cout << std::endl;
    std::cout << op_name << std::endl;
    std::cout << "Total time taken: " << total_time << " seconds" << std::endl;
    std::cout << "Throughput: " << throughput << " queries/second" << std::endl;
    metrics[op_name]["time"] = total_time;
    metrics[op_name]["throughput"] = throughput;
    metrics[op_name]["profiler"] = openMap.profiler.return_metrics();
}

nlohmann::json execute_benchmark(HashMap<uint32_t, uint32_t> &openMap, int GROUP_SIZE, int AMAC_REQUEST_SIZE, auto gen, auto dis)
{
    nlohmann::json results;
    measure_vectorized_operation(
        openMap, [&](auto &a, auto &b, auto &c)
        { openMap.vectorized_get_amac(a, b, c); },
        "Vectorized_get_amac()", AMAC_REQUESTS_SIZE, gen, dis, results);
    measure_vectorized_operation(
        openMap, [&](auto &a, auto &b, auto &c)
        { openMap.vectorized_get_coroutine(a, b, c); },
        "Vectorized_get_co()", AMAC_REQUESTS_SIZE, gen, dis, results);
    measure_vectorized_operation(
        openMap, [&](auto &a, auto &b, auto &c)
        { openMap.vectorized_get_gp(a, b); },
        "Vectorized_get_gp()", GROUP_SIZE, gen, dis, results);
    measure_vectorized_operation(
        openMap, [&](auto &a, auto &b, auto &c)
        { openMap.vectorized_get(a, b); },
        "Vectorized_get()", GROUP_SIZE, gen, dis, results);
    measure_vectorized_operation(
        openMap, [&](auto &a, auto &b, auto &c)
        { openMap.vectorized_get_coroutine_exp(a, b, c); },
        "vectorized_get_coroutine_exp()", AMAC_REQUESTS_SIZE, gen, dis, results);
    measure_vectorized_operation(
        openMap, [&](auto &a, auto &b, auto &c)
        { openMap.profile_vectorized_get_coroutine_exp(a, b, c); },
        "profile_vectorized_get_coroutine_exp()", AMAC_REQUESTS_SIZE, gen, dis, results);
    return results;
};

int main(int argc, char **argv)
{
    auto &manager = Prefetching::get().numa_manager;
    auto &benchmark_config = Prefetching::get().runtime_config;
    // clang-format off
    benchmark_config.add_options()
        ("d,distribution", "Type of distribution", cxxopts::value<std::vector<std::string>>()->default_value("uniform,zipfian"))
        ("number_keys", "Number of keys to fill the hashmap with", cxxopts::value<std::vector<long>>()->default_value("10000000"))
        ("number_buckets", "Number of buckets in the hashmap", cxxopts::value<std::vector<size_t>>()->default_value("500000"));
    // clang-format on
    benchmark_config.parse(argc, argv);

    int benchmark_run = 0;
    for (auto &runtime_config : benchmark_config.get_runtime_configs())
    {
        auto num_keys = convert<long>(runtime_config["number_keys"]);

        PrefetchProfiler profiler{30};
        StaticNumaMemoryResource mem_res{0};
        HashMap<uint32_t, uint32_t> openMap{convert<size_t>(runtime_config["number_buckets"]), profiler, mem_res};

        nlohmann::json results;
        std::random_device rd;
        std::mt19937 gen(rd());

        std::uniform_int_distribution<> uniform_dis(0, num_keys - 1);

        zipfian_int_distribution<int>::param_type p(0, num_keys - 1, 0.99, 27.000);
        zipfian_int_distribution<int> zipfian_distribution(p);

        for (uint32_t i = 0; i < num_keys; i++)
        {
            openMap.insert(i, i + 1);
        }

        if (runtime_config["distribution"] == "uniform")
        {
            std::cout << "----- Measuring Uniform Accesses -----" << std::endl;
            results["uniform"] = execute_benchmark(openMap, GROUP_SIZE, AMAC_REQUESTS_SIZE, gen, uniform_dis);
        }
        else if (runtime_config["distribution"] == "zipfian")
        {
            std::cout << "----- Measuring Zipfian Accesses -----" << std::endl;
            results["zipfian"] = execute_benchmark(openMap, GROUP_SIZE, AMAC_REQUESTS_SIZE, gen, zipfian_distribution);
        }
        else
        {
            std::cout << "Unknown Distribution Defined: " << runtime_config["distribution"] << std::endl;
        }

        auto results_file = std::ofstream{"hashmap_benchmark_" + std::to_string(benchmark_run++) + ".json"};
        results_file << results.dump(-1) << std::flush;
    }

    return 0;
}