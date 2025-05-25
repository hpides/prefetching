#include <unordered_map>

#include "cxxopts.hpp"

#include "runtime_config.hpp"

std::vector<ConfigMap> unpack_configuration_cross_product(ParsingResultIterator parse_result_it, const ParsingResultIterator &end_it, ConfigMap unpacked_result = {})
{
    auto runtime_configs = std::vector<ConfigMap>{};
    if (parse_result_it == end_it)
    {
        return std::vector<ConfigMap>{unpacked_result};
    }
    auto values = parse_result_it->as<std::vector<std::string>>();
    auto key = parse_result_it->key();
    parse_result_it++;
    for (auto &value : values)
    {
        unpacked_result[key] = value;
        auto deeper_unpacked_results = unpack_configuration_cross_product(parse_result_it, end_it, unpacked_result);
        runtime_configs.insert(runtime_configs.end(), deeper_unpacked_results.begin(), deeper_unpacked_results.end());
    }
    return runtime_configs;
}

void RuntimeConfig::parse(int argc, char **argv)
{
    auto res = options.parse(argc, argv);
    runtime_configs = unpack_configuration_cross_product(res.begin(), res.end());
}

std::vector<ConfigMap> &RuntimeConfig::get_runtime_configs()
{
    return runtime_configs;
}
