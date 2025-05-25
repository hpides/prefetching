#include <stdint.h>

#include <random>
#include <iostream>
#include <nlohmann/json.hpp>
#include <map>
#include <fstream>

#include "../lib/utils/utils.cpp"

const uint64_t GIBIBYTE = 1024 * 1024 * 1024;
const uint64_t MIBIBYTE = 1024 * 1024;
const uint64_t REPETITIONS = 1000;
const uint64_t LINEAR_AHEAD_LOADS = 64;
const uint64_t PADDING_END = 3;
const uint64_t DATA_SIZE = GIBIBYTE;

uint8_t data[DATA_SIZE];

uint64_t measure_prefetch_latency(const void *ptr)
{
    uint64_t start, end;

    start = read_cycles();
    lfence();
    asm volatile("" ::: "memory");

    __builtin_prefetch(ptr, 0, 3); // Prefetch to L1 cache

    asm volatile("" ::: "memory");
    lfence();
    end = read_cycles();

    return end - start;
}

uint64_t measure_load_latency(void *ptr, volatile uint64_t &dummy_sum)
{
    uint64_t start, end;

    start = read_cycles();
    lfence();
    asm volatile("" ::: "memory");

    dummy_sum = dummy_sum + *reinterpret_cast<uint64_t *>(ptr);

    asm volatile("" ::: "memory");
    lfence();
    end = read_cycles();

    return end - start;
}

uint64_t measure_prefetch_latency_verbose(const void *ptr)
{
    uint64_t latency = measure_prefetch_latency(ptr);
    const uint64_t cache_hit_threshold = 37;
    if (latency < cache_hit_threshold)
    {
        std::cout << "Data was in cache (cache hit)" << std::endl;
    }
    else
    {
        std::cout << "Data was not in cache (cache miss)" << std::endl;
    }
    printf("Prefetch latency: %lu cycles\n", latency);
    return latency;
}

uint64_t measure_load_latency_verbose(void *ptr, volatile uint64_t &dummy_sum)
{
    auto lat = measure_load_latency(ptr, dummy_sum);
    std::cout << "Load Latency was: " << lat << std::endl;
    return lat;
}

int main()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(LINEAR_AHEAD_LOADS, DATA_SIZE - 1 - PADDING_END);

    std::vector<uint64_t> latencies_uncached_prefetch;
    std::vector<uint64_t> latencies_uncached_load;
    std::vector<uint64_t> latencies_cached_prefetch;
    std::vector<uint64_t> latencies_cached_load;
    latencies_uncached_prefetch.reserve(REPETITIONS);
    latencies_uncached_load.reserve(REPETITIONS);
    latencies_cached_prefetch.reserve(REPETITIONS);
    latencies_cached_load.reserve(REPETITIONS);

    for (int i = 0; i < REPETITIONS; i++)
    {
        int random_number = dis(gen);
        uint64_t latency = measure_prefetch_latency_verbose(&data[random_number]); // most likely uncached data
        latencies_uncached_prefetch.push_back(latency);
    }

    std::cout << "-------    Expecting Cache Hits now -------" << std::endl;
    volatile uint64_t dummy_sum = 0;
    for (int i = 0; i < REPETITIONS; i++)
    {
        int random_number = dis(gen);
        auto ptr = &data[random_number];
        __builtin_prefetch(ptr, 0, 3); // Prefetch to L1 cache

        for (int i = 0; i < LINEAR_AHEAD_LOADS; ++i)
        {
            dummy_sum = dummy_sum + data[random_number - LINEAR_AHEAD_LOADS + i]; // attempt to also trigger hardware prefetcher
        }
        wait_cycles(200);
        uint64_t latency = measure_prefetch_latency_verbose(ptr); // most likely cached data
        latencies_cached_prefetch.push_back(latency);
    }

    std::cout << "------- Measuring Load Latencies now -------" << std::endl;

    for (int i = 0; i < REPETITIONS; i++)
    {
        int random_number = dis(gen);
        uint64_t latency = measure_load_latency_verbose(&data[random_number], dummy_sum); // most likely uncached data
        latencies_uncached_load.push_back(latency);
    }

    std::cout << "-------    Expecting Cache Hits now -------" << std::endl;
    for (int i = 0; i < REPETITIONS; i++)
    {
        int random_number = dis(gen);
        auto ptr = &data[random_number];
        __builtin_prefetch(ptr, 0, 3); // Prefetch to L1 cache

        for (int i = 0; i < LINEAR_AHEAD_LOADS; ++i)
        {
            dummy_sum = dummy_sum + data[random_number - LINEAR_AHEAD_LOADS + i]; // attempt to also trigger hardware prefetcher
        }
        wait_cycles(200);
        uint64_t latency = measure_load_latency_verbose(ptr, dummy_sum); // most likely cached data
        latencies_cached_load.push_back(latency);
    }

    nlohmann::json test = {{"latencies_uncached_prefetch", latencies_uncached_prefetch},
                           {"latencies_uncached_load", latencies_uncached_load},
                           {"latencies_cached_prefetch", latencies_cached_prefetch},
                           {"latencies_cached_load", latencies_cached_load}};

    auto results_file = std::ofstream{"prefetch_latencies.json"};
    results_file << test.dump(-1) << std::flush;
    return 0;
}