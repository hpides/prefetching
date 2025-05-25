#include <perfcpp/event_counter.h>
#include <iostream>
#include <thread>

#include "prefetching.hpp"
#include "../../config.hpp"

int main()
{
    auto &perf_manager = Prefetching::get().perf_manager;
    auto counter_definitions = perf::CounterDefinition{};
    auto event_counter = perf::EventCounter{counter_definitions};
    std::vector<std::jthread> threads;
    /// Add performance counters.
    event_counter.add({"instructions", "cycles", "cache-misses"});

    event_counter.start();
    for (size_t i = 0; i < 16; ++i)
    {
        threads.emplace_back([&, i]()
                             { std::cout << "Thread " << std::to_string(i) << " ran." << std::endl; });
    }
    for (auto &t : threads)
    {
        t.join();
    }
    event_counter.stop();

    const auto result = event_counter.result();
    for (const auto [name, value] : result)
    {
        std::cout << name << ": " << value << std::endl;
    }
    perf_manager.initialize_counter_definition(get_default_perf_config_file());
    auto st_event_counter = perf_manager.get_event_counter();
    std::cout << " === starting custom perf profiling === " << std::endl;
    st_event_counter.start();
    const size_t array_size = 500000000;
    std::vector<int> data(array_size);

    std::fill(data.begin(), data.end(), 1);

    volatile long long sum = 0;
    for (auto num : data)
    {
        sum = sum + num;
    }
    st_event_counter.stop();
    const auto wrapped_result = st_event_counter.result(array_size);
    for (const auto [name, value] : wrapped_result)
    {
        std::cout << name << ": " << value << std::endl;
    }

    auto mt_event_counter = perf_manager.get_mt_event_counter(16);
    threads.clear();
    for (size_t i = 0; i < 16; ++i)
    {
        threads.emplace_back([&, i]()
                             {
            mt_event_counter.start(i);
            std::cout << "Thread " << std::to_string(i) << " ran." << std::endl;
            mt_event_counter.stop(i); });
    }

    for (auto &t : threads)
    {
        t.join();
    }
    const auto mt_wrapped_result = mt_event_counter.result(array_size);
    for (const auto [name, value] : mt_wrapped_result)
    {
        std::cout << name << ": " << value << std::endl;
    }
    return 0;
}
