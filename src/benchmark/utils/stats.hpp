#include <string>
#include "../../lib/utils/utils.hpp"

void generate_stats(auto &results, auto &durations, std::string prefix)
{
    results["min_" + prefix + "runtime"] = *std::min_element(durations.begin(), durations.end());
    results["median_" + prefix + "runtime"] = findMedian(durations, durations.size());
    results[prefix + "runtime"] = results["median_" + prefix + "runtime"];
    results[prefix + "runtimes"] = durations;
}
