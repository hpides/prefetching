#include <filesystem>
#include <iostream>
#include <perfcpp/hardware_info.h>
#include <perfcpp/event_counter.h>
#include <nlohmann/json.hpp>

class PerfManager
{
public:
    PerfManager();
    void initialize_counter_definition(const std::string &config_file);
    template <typename EventCounterType>
    void result(EventCounterType &event_counter, nlohmann::json &results, std::uint64_t normalization = 1U);
    template <typename EventCounterType>
    void result(EventCounterType &event_counter, nlohmann::json &results, std::string description, std::uint64_t normalization = 1U);
    perf::EventCounter get_event_counter();
    perf::MultiThreadEventCounter get_mt_event_counter(uint16_t num_threads);

private:
    perf::CounterDefinition _counter_definition;
    std::vector<std::string> _additional_selected_counters; // names of counters read from config file
    // some additional counters are set in the initialization, depending on system support
    std::vector<std::string> _selected_default_counters = {
        "cycles",
        "cpu-clock",
        "instructions",
        "cache-misses",
        "dTLB-load-misses",
        "page-faults",
        "alignment-faults",
        "cache-references",
        "L1-dcache-loads",
        "L1-dcache-load-misses",
        "L1-data-miss-ratio",
        "cycles-per-instruction",
    };
    const size_t _max_groups = 15;
    const size_t _max_counter_per_group = 4;
};