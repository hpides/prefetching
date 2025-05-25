#include "perf_manager.hpp"

#include "../../config.hpp"

PerfManager::PerfManager()
{
    std::string hostname = get_curr_hostname();
    if (!perf::HardwareInfo::is_intel() && !(hostname == "fabian-E15"))
    {
        _selected_default_counters.push_back("stalled-cycles-backend");
    }
    if (!hostname.starts_with("ca"))
    {
        _selected_default_counters.push_back("dTLB-loads");
        _selected_default_counters.push_back("dTLB-miss-ratio");
    }
}

void add_counters(auto &event_counter, auto &default_counters, auto additiontal_counters)
{
    if (!event_counter.add(default_counters))
    {
        throw std::runtime_error("Unable to load default Counters.");
    };
    if (!event_counter.add(additiontal_counters))
    {
        throw std::runtime_error("Could not add all additional Counters.");
    }
}

perf::EventCounter PerfManager::get_event_counter()
{
    auto config = perf::Config();
    config.include_child_threads(true); // profiles for all child threads as well.
    config.max_groups(_max_groups);
    auto event_counter = perf::EventCounter{_counter_definition, config};
    add_counters(event_counter, _selected_default_counters, _additional_selected_counters);
    return event_counter;
}

perf::MultiThreadEventCounter PerfManager::get_mt_event_counter(uint16_t num_threads)
{
    auto config = perf::Config();
    config.max_groups(_max_groups);
    auto mt_event_counter = perf::MultiThreadEventCounter{_counter_definition, num_threads, config};
    add_counters(mt_event_counter, _selected_default_counters, _additional_selected_counters);
    return mt_event_counter;
}

void PerfManager::initialize_counter_definition(const std::string &config_file)
{
    const std::filesystem::path config_file_path{config_file};
    if (!std::filesystem::exists(config_file_path))
    {
        throw std::runtime_error("Perf config file " + config_file + " could not be found.");
    }
    _counter_definition.read_counter_configuration(config_file);

    auto additional_counter_definition = perf::CounterDefinition();
    additional_counter_definition.clear_counter_configuration(); // we don't want the base definitions here, only the counters defined in the config file.
    additional_counter_definition.read_counter_configuration(config_file);
    _additional_selected_counters = additional_counter_definition.names();

    if (additional_counter_definition.names().size() + _selected_default_counters.size() > _max_groups * _max_counter_per_group)
    {
        throw std::runtime_error(
            "Too many counters have been defined in " + config_file +
            " (" + std::to_string(_counter_definition.names().size()) + ").");
    }
}

template <typename EventCounterType>
void PerfManager::result(EventCounterType &event_counter, nlohmann::json &results, std::uint64_t normalization)
{
    result<EventCounterType>(event_counter, results, "", normalization);
}

template <typename EventCounterType>
void PerfManager::result(EventCounterType &event_counter, nlohmann::json &results, std::string description, std::uint64_t normalization)
{
    auto counter_result = event_counter.result(normalization);
    for (const auto &[counter_name, counter_value] : counter_result)
    {
        std::string string_counter_name(counter_name);
        if (description == "")
        {
            results["perf"][string_counter_name] = counter_value;
        }
        else
        {
            results["perf"][description][string_counter_name] = counter_value;
        }
    }
}

template void PerfManager::result<perf::EventCounter>(perf::EventCounter &, nlohmann::json &, std::uint64_t);
template void PerfManager::result<perf::EventCounter>(perf::EventCounter &, nlohmann::json &, std::string, std::uint64_t);
template void PerfManager::result<perf::MultiThreadEventCounter>(perf::MultiThreadEventCounter &, nlohmann::json &, std::uint64_t);
template void PerfManager::result<perf::MultiThreadEventCounter>(perf::MultiThreadEventCounter &, nlohmann::json &, std::string, std::uint64_t);
