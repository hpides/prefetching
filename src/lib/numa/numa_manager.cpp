#include <stdexcept>
#include <fstream>
#include <utility>
#include <sstream>
#include "numa.h"
#include <iostream>

#include "numa_manager.hpp"

std::string trim(const std::string &str)
{
    size_t first = str.find_first_not_of(" \t");
    size_t last = str.find_last_not_of(" \t");
    return (first == std::string::npos || last == std::string::npos) ? "" : str.substr(first, last - first + 1);
}

std::string join(const auto &vec, const std::string &delimiter)
{
    std::ostringstream oss;
    if (!vec.empty())
    {
        oss << vec[0];
        for (size_t i = 1; i < vec.size(); ++i)
        {
            oss << delimiter << vec[i];
        }
    }
    return oss.str();
}

void warn_single_threaded()
{
    static const auto runOnce = []
    { std::cout << "\033[1;31m[WARNING] /proc/cpuinfo: missing core id for processor. Assuming core_id = processor_id (no multi threading).\033[0m" << std::endl; return true; }();
}

std::vector<std::pair<NodeID, NodeID>> cpu_to_core_mappings(const std::string &filename = "/proc/cpuinfo")
{
    std::vector<std::pair<NodeID, NodeID>> processors;
    std::ifstream cpuinfo(filename);
    std::string line;
    std::pair<NodeID, NodeID> curr_cpu_info = {std::numeric_limits<NodeID>::max(), std::numeric_limits<NodeID>::max()}; // cpu, core
    if (!cpuinfo.is_open())
    {
        throw std::runtime_error("Failed to open " + filename);
    }

    while (std::getline(cpuinfo, line))
    {
        std::string::size_type pos = line.find(':');
        if (pos == std::string::npos)
            continue;

        std::string key = trim(line.substr(0, pos));
        std::string value_str = trim(line.substr(pos + 1));

        if (key == "processor")
        {
            if (curr_cpu_info.first != std::numeric_limits<NodeID>::max())
            {
                if (curr_cpu_info.second == std::numeric_limits<NodeID>::max())
                {
                    warn_single_threaded();
                    curr_cpu_info.second = curr_cpu_info.first;
                }
                processors.emplace_back(curr_cpu_info);
            }
            curr_cpu_info.first = static_cast<NodeID>(std::stoi(value_str));
            curr_cpu_info.second = std::numeric_limits<NodeID>::max();
        }
        else if (key == "core id")
        {
            curr_cpu_info.second = static_cast<NodeID>(std::stoi(value_str));
        }
    }

    if (curr_cpu_info.first != std::numeric_limits<NodeID>::max())
    {
        if (curr_cpu_info.second == std::numeric_limits<NodeID>::max())
        {
            warn_single_threaded();
            curr_cpu_info.second = curr_cpu_info.first;
        }
        processors.emplace_back(curr_cpu_info);
    }

    return processors;
}

NumaManager::NumaManager()
{
    if (numa_available() < 0)
    {
        throw std::runtime_error("Numa library not available.");
    }

    if (numa_max_node() == 0)
    {
        std::cout << std::endl
                  << "\033[1;31m[WARNING]: only one NUMA node identified, NUMA bindings won't have any effect.\033[0m" << std::endl
                  << std::endl;
    }
    init_topology_info();
    print_topology();
    interleaving_memory_resource.emplace(number_nodes);
}

void NumaManager::init_topology_info()
{
    number_available_cpus = numa_num_task_cpus();
    number_cpus = numa_num_configured_cpus();
    number_nodes = numa_num_configured_nodes();
    cpu_to_node.resize(number_cpus, UNDEFINED_NODE);
    node_to_cpus.resize(number_nodes);
    node_to_available_cpus.resize(number_nodes);
    for (NodeID numa_cpu = 0; numa_cpu < number_cpus; ++numa_cpu)
    {
        NodeID numa_node = numa_node_of_cpu(numa_cpu);
        cpu_to_node[numa_cpu] = numa_node;
        node_to_cpus[numa_node].push_back(numa_cpu);
        if (numa_bitmask_isbitset(numa_all_cpus_ptr, numa_cpu))
        {
            node_to_available_cpus[numa_node].push_back(numa_cpu);
        }
    }
    for (NodeID numa_node = 0; numa_node < number_nodes; ++numa_node)
    {
        if (node_to_available_cpus[numa_node].size())
        {
            active_nodes.push_back(numa_node);
        }
    }
    auto cpu_to_core = cpu_to_core_mappings();
    NodeID max_core = 0;
    for (const auto &[cpu, core] : cpu_to_core)
    {
        max_core = std::max(max_core, core);
    }
    socket_and_core_id_to_cpu.resize(number_nodes);
    socket_and_core_id_to_available_cpu.resize(number_nodes);
    for (NodeID node = 0; node < socket_and_core_id_to_available_cpu.size(); node++)
    {
        socket_and_core_id_to_available_cpu[node].resize(max_core + 1);
        socket_and_core_id_to_cpu[node].resize(max_core + 1);
    }
    cpu_to_core_id.resize(number_cpus);
    for (const auto &[cpu, core] : cpu_to_core)
    {
        socket_and_core_id_to_cpu[cpu_to_node[cpu]][core].emplace_back(cpu);
        if (numa_bitmask_isbitset(numa_all_cpus_ptr, cpu))
        {
            socket_and_core_id_to_available_cpu[cpu_to_node[cpu]][core].emplace_back(cpu);
        }

        cpu_to_core_id[cpu] = core;
    }

    auto allow_mem_nodes = numa_get_mems_allowed();
    for (NodeID node = 0; node < number_nodes; node++)
    {
        if (numa_bitmask_isbitset(allow_mem_nodes, node))
        {
            mem_nodes.emplace_back(node);
        }
    }
    numa_bitmask_free(allow_mem_nodes);
}

void NumaManager::print_topology()
{
    auto allow_mem_nodes = numa_get_mems_allowed();
    std::cout << " --- NUMA Topology Information --- " << std::endl;
    std::cout << "number_cpus: " << number_cpus << "/" << numa_num_configured_cpus() << std::endl;
    std::cout << "number_nodes: " << number_nodes << "/" << numa_num_configured_nodes() << std::endl;
    for (NodeID node = 0; node < number_nodes; node++)
    {
        std::cout << "Node [" << node << "] (mem allowed=" << numa_bitmask_isbitset(allow_mem_nodes, node) << ") : ";
        for (NodeID core = 0; core < socket_and_core_id_to_cpu[node].size(); core++)
        {
            std::cout << "{" << join(socket_and_core_id_to_available_cpu[node][core], ",") << "/" << join(socket_and_core_id_to_cpu[node][core], ",") << "} ";
        }
        std::cout << std::endl;
    }

    numa_bitmask_free(allow_mem_nodes);
}