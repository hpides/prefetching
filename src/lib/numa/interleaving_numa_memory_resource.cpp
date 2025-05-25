#include <cstddef>
#include <memory_resource>
#include <numaif.h>
#include <numa.h>
#include <iostream>

#include <jemalloc/jemalloc.h>

#include "interleaving_numa_memory_resource.hpp"

const auto ACTUAL_PAGE_SIZE = get_page_size();

InterleavingNumaMemoryResource::InterleavingNumaMemoryResource(NodeID num_numa_nodes, bool use_explicit_huge_pages, bool use_madvise_huge_pages) : num_numa_nodes_(num_numa_nodes), NumaMemoryResource(use_explicit_huge_pages, use_madvise_huge_pages){};

// There is a limit to the number of memory areas a process can have (cat /proc/sys/vm/max_map_count)
// which defaults to 65536. If we were to interleave at page granularity, we create a new memory area,
// per page, which would limit our memory to ~256MiB. Thus if we were to manually interleave based on the
// pointer alone, we must choose a higher granularity. Interleaving every 512th page leaves us with roughly
// 128 GiB of Memory. If for some reason, still to many areas are created, the mbind call will likely fail
// with: "mbind failed with -1 errno: Cannot allocate memory"
NodeID InterleavingNumaMemoryResource::node_id(void *p)
{
    return (reinterpret_cast<uint64_t>(p) >> 21) % num_numa_nodes_;
}

void InterleavingNumaMemoryResource::move_pages_policed(void *p, size_t size)
{
    const auto max_node = numa_max_node();

    if (max_node == 0)
    {
        return;
    }
    auto bitmask = numa_bitmask_alloc(max_node + 1);
    numa_bitmask_clearall(bitmask);

    size_t curr_size = ACTUAL_PAGE_SIZE;
    char *last_start = reinterpret_cast<char *>(p);
    NodeID curr_node_id = node_id(reinterpret_cast<char *>(p));
    unsigned long num_pages = calculate_allocated_pages(size);
    for (int i = 1; i < num_pages; ++i)
    {
        const auto target_node_id = node_id(reinterpret_cast<char *>(p) + (i * ACTUAL_PAGE_SIZE));
        if (target_node_id != curr_node_id)
        {
            numa_bitmask_setbit(bitmask, curr_node_id);
            auto ret = mbind(last_start, curr_size, MPOL_BIND, bitmask->maskp, bitmask->size + 1, 0);
            if (ret != 0)
            {
                throw std::runtime_error("mbind failed with " + std::to_string(ret) + " errno: " + strerror(errno));
            }
            numa_bitmask_clearbit(bitmask, curr_node_id);

            last_start = reinterpret_cast<char *>(p) + (i * ACTUAL_PAGE_SIZE);
            curr_size = ACTUAL_PAGE_SIZE;
            curr_node_id = target_node_id;
        }
        else
        {
            curr_size += ACTUAL_PAGE_SIZE;
        }
    }

    numa_bitmask_setbit(bitmask, curr_node_id);
    auto ret = mbind(last_start, curr_size, MPOL_BIND, bitmask->maskp, bitmask->size + 1, 0);
    if (ret != 0)
    {
        throw std::runtime_error("mbind failed with " + std::to_string(ret) + " errno: " + strerror(errno));
    }
    numa_bitmask_clearbit(bitmask, curr_node_id);
    numa_bitmask_free(bitmask);
}