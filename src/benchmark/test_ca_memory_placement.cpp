#include "prefetching.hpp"

#include <algorithm>
#include <random>
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>
#include <numaif.h>
#include <numa.h>

#include "numa/numa_memory_resource.hpp"
#include "numa/static_numa_memory_resource.hpp"
#include "utils/utils.hpp"
#include "../../third_party/tinymembench/tinymembench.h"
#include "../../third_party/tinymembench/util.h"
#include "../../config.hpp"
#include "../lib/utils/simple_continuous_allocator.hpp"
#include "utils/fujitsu_memory_allocation.hpp"

std::vector<long long> get_free_memory_per_node()
{
    // Ensure the system supports NUMA
    if (numa_available() == -1)
    {
        throw std::runtime_error("NUMA is not available on this system.");
    }

    int num_nodes = numa_max_node() + 1; // Total number of NUMA nodes
    std::vector<long long> free_memory(num_nodes, 0);

    for (int node = 0; node < num_nodes; ++node)
    {
        long long free_ram = 0;
        long long total_ram = numa_node_size64(node, &free_ram);
        if (total_ram == -1)
        {
            std::cerr << "Error retrieving information for node " << node << std::endl;
            free_memory[node] = -1; // Indicate an error for this node
        }
        else
        {
            free_memory[node] = free_ram;
        }
    }

    return free_memory;
}

std::vector<int> get_numa_node_locations(void *memory, size_t size)
{
    // Calculate the number of pages in the memory region
    size_t page_size = 2 << 20;
    size_t num_pages = (size + page_size - 1) / page_size; // Round up to the nearest page

    std::vector<void *> page_addresses(num_pages);
    for (size_t i = 0; i < num_pages; ++i)
    {
        page_addresses[i] = static_cast<char *>(memory) + i * page_size;
    }

    // Prepare a vector to store the NUMA node of each page
    std::vector<int> node_locations(num_pages, -1); // Initialize with -1 (unknown)

    // Call move_pages with null target nodes to retrieve NUMA locations
    int result = move_pages(0,                     // Current process (PID 0)
                            num_pages,             // Number of pages to query
                            page_addresses.data(), // Addresses of pages
                            nullptr,               // No migration, just query
                            node_locations.data(), // Output NUMA node locations
                            0);                    // No special flags

    if (result != 0)
    {
        perror("move_pages");
        throw std::runtime_error("move_pages failed to retrieve NUMA locations.");
    }

    return node_locations;
}

struct LBenchmarkConfig
{
    int memory_size;
    NodeID alloc_on_node;
};

int main(int argc, char **argv)
{
    auto &benchmark_config = Prefetching::get().runtime_config;
    // clang-format off
    benchmark_config.add_options()
        ("memory_size", "Total memory allocated MiB", cxxopts::value<std::vector<int>>()->default_value("1024"))
        ("alloc_on_node", "Allocate on node", cxxopts::value<std::vector<NodeID>>()->default_value("0"))
        ("out", "Path on which results should be stored", cxxopts::value<std::vector<std::string>>()->default_value("test.json"));
    // clang-format on
    benchmark_config.parse(argc, argv);

    int benchmark_run = 0;

    std::vector<nlohmann::json> all_results;
    for (auto &runtime_config : benchmark_config.get_runtime_configs())
    {
        auto memory_size = convert<int>(runtime_config["memory_size"]);
        auto alloc_on_node = convert<NodeID>(runtime_config["alloc_on_node"]);
        LBenchmarkConfig config = {
            memory_size,
            alloc_on_node,
        };

        const auto &nm = Prefetching::get().numa_manager;
        volatile auto mem_filler = fill_numa_nodes_except(config.alloc_on_node);
        if (Prefetching::get().numa_manager.node_to_available_cpus[alloc_on_node].size() > 0)
        {
            pin_to_cpu(Prefetching::get().numa_manager.node_to_available_cpus[alloc_on_node].back());
        }
        auto memRes = StaticNumaMemoryResource(alloc_on_node, false, true);
        auto simpleContinuousAllocator = SimpleContinuousAllocator(memRes, 2048l * (2 << 20), 512l * (1 << 20));

        size_t region_size = 1 * (2l << 30);
        void *memory_region = simpleContinuousAllocator.allocate(region_size);
        void *memory_region_2 = simpleContinuousAllocator.allocate(region_size);
        memset(memory_region, 0, region_size);
        memset(memory_region_2, 0, region_size);

        auto numa_locations = get_numa_node_locations(memory_region, region_size);
        auto numa_locations_2 = get_numa_node_locations(memory_region_2, region_size);

        std::cout << "numa_locations: ";
        for (auto node : numa_locations)
        {
            std::cout << node << ", ";
        }
        std::cout << std::endl;

        std::cout << "numa_locations_2: ";
        for (auto node : numa_locations_2)
        {
            std::cout << node << ", ";
        }
        std::cout << std::endl;

        volatile int sum = 0;
        for (unsigned i = 0; i < memory_size; i += 4)
        {
            sum = sum + reinterpret_cast<int32_t *>(memory_region)[i];
        }
    }

    return 0;
}
