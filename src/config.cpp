#include "config.hpp"

std::string get_curr_hostname()
{
    char c_hostname[HOST_NAME_MAX]; // HOST_NAME_MAX is typically defined to be 255
    if (gethostname(c_hostname, sizeof(c_hostname)) != 0)
    {
        throw std::runtime_error("Could not get hostname.");
    }
    std::string hostname(c_hostname);
    return hostname;
}

template <typename T>
T get_config_entry(std::string hostname, const std::unordered_map<std::string, T> &config)
{
    if (!config.contains(hostname))
    {
        throw std::runtime_error("config does not contain " + hostname + " entry (config.hpp).");
    }
    return config.at(hostname);
}
template NumaConfig get_config_entry<NumaConfig>(std::string hostname, const std::unordered_map<std::string, NumaConfig> &config);
template std::string get_config_entry<std::string>(std::string hostname, const std::unordered_map<std::string, std::string> &config);

template <typename T>
T get_config_entry_default(std::string hostname, const std::unordered_map<std::string, T> &config, T default_entry)
{
    if (!config.contains(hostname))
    {
        return default_entry;
    }
    return config.at(hostname);
}
template NumaConfig get_config_entry_default<NumaConfig>(std::string hostname, const std::unordered_map<std::string, NumaConfig> &config, NumaConfig default_entry);
template std::string get_config_entry_default<std::string>(std::string hostname, const std::unordered_map<std::string, std::string> &config, std::string default_entry);

std::string get_default_perf_config_file()
{
    return default_repository_path + "/src/perf_configs/generic_selections/" + get_config_entry(get_curr_hostname(), HOST_TO_PERF_CONFIG_FILE);
}