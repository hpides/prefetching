#include <chrono>
#include <thread>
#include <string>

#include "../../lib/utils/utils.hpp"
#include "../../lib/prefetching.hpp"
#include "../../lib/numa/static_numa_memory_resource.hpp"

std::vector<std::unique_ptr<StaticNumaMemoryResource>> fill_numa_nodes_except(NodeID alloc_target_node, double target_allocation_rate = 0.66)
{
    auto memory_stats = get_memory_stats_per_numa_node();
    std::vector<std::unique_ptr<StaticNumaMemoryResource>> filler_mem_resources;

    for (NodeID i = 0; i <= numa_max_node(); ++i)
    {
        auto resource = std::make_unique<StaticNumaMemoryResource>(i, false, false);

        if (i != alloc_target_node)
        {
            auto &[total_nn_mem, available_nn_mem] = memory_stats[i];
            long long target_available = total_nn_mem - (target_allocation_rate * total_nn_mem);
            if (available_nn_mem > target_available)
            {
                void *ptr = resource->do_allocate(available_nn_mem - target_available, 16);
                filler_mem_resources.emplace_back(std::move(resource));
                std::this_thread::sleep_for(std::chrono::seconds(3));
                memset(ptr, 0, available_nn_mem - target_available);
            }
        }
    }
    return filler_mem_resources;
}

std::vector<std::unique_ptr<StaticNumaMemoryResource>> fujitsu_conditional_memory_preparation(NodeID alloc_target_node, bool transparent_huge_pages, std::string hostname)
{
    if (transparent_huge_pages && hostname.starts_with("ca"))
    {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return fill_numa_nodes_except(alloc_target_node);
    }
    return {};
}