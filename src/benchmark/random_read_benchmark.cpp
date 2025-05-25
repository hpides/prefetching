#include "random_access.hpp"

#include <random>
#include <functional>
#include <chrono>
#include <assert.h>
#include "zipfian_int_distribution.hpp"
#include <iostream>

const int NUM_VALUES = 6'000'000; // ~ roughly 24MB seems to be the sweet-spot for low tlb misses, high cache misses
const int TOTAL_QUERIES = 25'000'000;
const int GROUP_SIZE = 32;
const int AMAC_REQUESTS_SIZE = 1024;

template <typename Function>
void measure_vectorized_operation(RandomAccess<uint8_t> &random_access, Function func, const std::string &op_name, int invoke_vector_size, auto gen, auto dis)
{
    auto start = std::chrono::high_resolution_clock::now();
    auto end = std::chrono::high_resolution_clock::now();
    double total_time = 0;

    std::vector<size_t> requests(invoke_vector_size);
    std::vector<uint8_t> results(invoke_vector_size);
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
            if (!(results.at(j) == (requests.at(j) % 256)))
            {
                perror("random read returned wrong value.");
                exit(-1);
            }
        }
    }

    double throughput = TOTAL_QUERIES / total_time;

    std::cout << std::endl;
    std::cout << op_name << std::endl;
    std::cout << "Total time taken: " << total_time << " seconds" << std::endl;
    std::cout << "Throughput: " << throughput << " queries/second" << std::endl;
}

void execute_benchmark(RandomAccess<uint8_t> &random_access, int GROUP_SIZE, int AMAC_REQUEST_SIZE, auto gen, auto dis)
{
    measure_vectorized_operation(
        random_access, [&](auto &a, auto &b, auto &c)
        { random_access.vectorized_get_amac(a, b, c); },
        "Vectorized_get_amac()", AMAC_REQUESTS_SIZE, gen, dis);
    measure_vectorized_operation(
        random_access, [&](auto &a, auto &b, auto &c)
        { random_access.vectorized_get_coroutine(a, b, c); },
        "Vectorized_get_co()", AMAC_REQUESTS_SIZE, gen, dis);
    measure_vectorized_operation(
        random_access, [&](auto &a, auto &b, auto &c)
        { random_access.vectorized_get_gp(a, b); },
        "Vectorized_get_gp()", GROUP_SIZE, gen, dis);
    measure_vectorized_operation(
        random_access, [&](auto &a, auto &b, auto &c)
        { random_access.vectorized_get(a, b); },
        "Vectorized_get()", GROUP_SIZE, gen, dis);
    measure_vectorized_operation(
        random_access, [&](auto &a, auto &b, auto &c)
        { random_access.vectorized_get_coroutine_exp(a, b, c); },
        "vectorized_get_coroutine_exp()", AMAC_REQUESTS_SIZE, gen, dis);
};

int main()
{
    RandomAccess<uint8_t> random_access{size_t{NUM_VALUES}};

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> uniform_dis(0, NUM_VALUES - 1);

    zipfian_int_distribution<int>::param_type p(1, 1e6, 0.99, 27.000);
    zipfian_int_distribution<int> zipfian_distribution(p);

    std::cout << "----- Measuring Uniform Accesses -----" << std::endl;
    execute_benchmark(random_access, GROUP_SIZE, AMAC_REQUESTS_SIZE, gen, uniform_dis);
    std::cout << "----- Measuring Zipfian Accesses -----" << std::endl;
    execute_benchmark(random_access, GROUP_SIZE, AMAC_REQUESTS_SIZE, gen, zipfian_distribution);

    return 0;
}