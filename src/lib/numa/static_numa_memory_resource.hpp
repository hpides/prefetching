#pragma once

#include <cstddef>
#include <memory_resource>

// #include <boost/container/pmr/memory_resource.hpp>
#include <jemalloc/jemalloc.h>

#include "numa_memory_resource.hpp"

class StaticNumaMemoryResource : public NumaMemoryResource
{
public:
    // Constructor creating an arena for a specific node.
    explicit StaticNumaMemoryResource(NodeID target_numa_node, bool use_explicit_huge_pages = false, bool madvise_huge_pages = false);

    NodeID node_id(void *p);
    void move_pages_policed(void *p, size_t size);

protected:
    const NodeID target_numa_node_;
};
