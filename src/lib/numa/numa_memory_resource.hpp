#pragma once

#include <cstddef>
#include <new>
#include <memory_resource>
#include <unistd.h>

// #include <boost/container/pmr/memory_resource.hpp>
#include <jemalloc/jemalloc.h>

#include "../types.hpp"

inline std::size_t get_page_size()
{
    return static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
}

inline std::size_t get_cache_line_size()
{
    auto cl_size = static_cast<std::size_t>(sysconf(_SC_LEVEL3_CACHE_LINESIZE));
    if (cl_size != 0)
    {
        return cl_size;
    }
    cl_size = static_cast<std::size_t>(sysconf(_SC_LEVEL2_CACHE_LINESIZE));
    if (cl_size != 0)
    {
        return cl_size;
    }
#if __cpp_lib_hardware_interference_size >= 201603
    cl_size = std::hardware_destructive_interference_size;
#endif
    if (cl_size == 0)
    {
        throw std::runtime_error("Cacheline size could not be determined.");
    }
    return cl_size;
}
constexpr size_t HUGE_PAGE_SIZE = 2 * 1024 * 1024;
std::size_t calculate_allocated_pages(size_t size);

/**
 * The base memory resource for NUMA memory allocation.
 *
 * We want a low overhead numa memory allocator, where we can tell on
 * which node data is stored by the pointer it self. Thus, initially a large
 * pool of memory is allocated aligned to the page size. Each page is then moved
 * to its specific node based on a simple (mathematic rule), e.g. (ptr / 4096) % num_nodes.
 */
class NumaMemoryResource : public std::pmr::memory_resource
{
public:
    // Constructor creating an arena for a specific node.
    explicit NumaMemoryResource(bool use_explicit_huge_pages = false, bool madvise_huge_pages = false);

    ~NumaMemoryResource();

    NumaMemoryResource(const NumaMemoryResource &) = delete;
    NumaMemoryResource &operator=(const NumaMemoryResource &) = delete;

    // Methods defined by memory_resource.
    void *do_allocate(std::size_t bytes, std::size_t alignment) override;

    /**
     * Entry point for deallocation behavior.
     */
    void do_deallocate(void *p, std::size_t bytes, std::size_t alignment) override;

    bool do_is_equal(const memory_resource &other) const noexcept override;

    virtual NodeID node_id(void *p) = 0;
    virtual void move_pages_policed(void *p, size_t size) = 0;

    static void *alloc(extent_hooks_t *extent_hooks, void *new_addr, size_t size, size_t alignment, bool *zero,
                       bool *commit, unsigned arena_index);

    static bool dalloc(extent_hooks_t *extent_hooks, void *addr, size_t size, bool committed, unsigned arena_ind);

protected:
    extent_hooks_t extentHooks_;
    bool _use_explicit_huge_pages;
    bool _madvise_huge_pages;
    int32_t _allocation_flags{0};
    int32_t arena_id;
};
