#pragma once

#include <stdint.h>
#include <numa.h>
#if defined(X86_64)
#include <x86intrin.h>
#elif defined(AARCH64)
#include <atomic>
#endif
#include <vector>
#include <random>
#include <chrono>
#include <iostream>
#include <algorithm>
#include <thread>
#include <codecvt>
#include <locale>

#include "../types.hpp"

enum prefetch_hint
{
    ET0 = 7,
    ET1 = 6,
    // we do not really care about above. Below are verified to be identical for x86 and ARM
    T0 = 3,
    T1 = 2,
    T2 = 1,
    NTA = 0
};

template <int locality>
inline void prefetch(const void *addr)
{
    __builtin_prefetch(addr, 0, locality);
    //                       ^ signals we want to load data, other options include writing, or loading instructions
};

void wait_cycles(uint64_t x);

void pin_to_cpu(NodeID cpu);
void pin_to_cpus(std::vector<NodeID> &cpus);

// {total_ram, free_ram}
std::vector<std::pair<long long, long long>> get_memory_stats_per_numa_node();

std::string convert_to_utf8(const std::string &str);

template <typename c>
c findMedian(std::vector<c> &container,
             int n)
{

    if (n % 2 == 0)
    {

        nth_element(container.begin(),
                    container.begin() + n / 2,
                    container.end());

        nth_element(container.begin(),
                    container.begin() + (n - 1) / 2,
                    container.end());

        return static_cast<c>((container[(n - 1) / 2] + container[n / 2]) / 2.0);
    }

    else
    {

        nth_element(container.begin(),
                    container.begin() + n / 2,
                    container.end());

        return container[n / 2];
    }
}

void initialize_pointer_chase(uint64_t *data, size_t size);

long long round_up_to_multiple(long long num, long long x);