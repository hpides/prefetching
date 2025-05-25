#pragma once

#include <cstddef>
#include <new>
#include <memory_resource>
#include <unistd.h>

#include <boost/container/pmr/memory_resource.hpp>

#include "../types.hpp"

/**
 * The base memory resource for NUMA memory allocation.
 *
 * We want a low overhead numa memory allocator, where we can tell on
 * which node data is stored by the pointer it self. Thus, initially a large
 * pool of memory is allocated aligned to the page size. Each page is then moved
 * to its specific node based on a simple (mathematic rule), e.g. (ptr / 4096) % num_nodes.
 */
class NumaMemoryResourceNoJemalloc : public std::pmr::memory_resource
{
public:
    // Constructor creating an arena for a specific node.
    explicit NumaMemoryResourceNoJemalloc(NodeID alloc_on_node, bool use_explicit_huge_pages = false, bool madvise_huge_pages = false);

    ~NumaMemoryResourceNoJemalloc();

    NumaMemoryResourceNoJemalloc(const NumaMemoryResourceNoJemalloc &) = delete;
    NumaMemoryResourceNoJemalloc &operator=(const NumaMemoryResourceNoJemalloc &) = delete;

    // Methods defined by memory_resource.
    void *do_allocate(std::size_t bytes, std::size_t alignment) override;

    /**
     * Entry point for deallocation behavior.
     */
    void do_deallocate(void *p, std::size_t bytes, std::size_t alignment) override;

    bool do_is_equal(const memory_resource &other) const noexcept override;

    void move_pages_policed(void *p, size_t size);

protected:
    bool _use_explicit_huge_pages;
    bool _madvise_huge_pages;
    NodeID _alloc_on_node;
    int32_t _allocation_flags{0};
};
