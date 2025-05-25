#include <vector>

#include "cxxopts.hpp"

using ConfigMap = std::unordered_map<std::string, std::string>;
using ParsingResultIterator = cxxopts::ParseResult::Iterator;

template <typename T>
T convert(std::string string)
{
    T result;
    cxxopts::values::parse_value(string, result);
    return result;
}

class RuntimeConfig
{
public:
    cxxopts::Options options = cxxopts::Options("Prefetching-Benchmark", "Set runtime options for prefetching benchmark");

    auto add_options() { return options.add_options(); }
    void parse(int argc, char **argv);
    std::vector<ConfigMap> &get_runtime_configs();

private:
    std::vector<ConfigMap> runtime_configs;
};