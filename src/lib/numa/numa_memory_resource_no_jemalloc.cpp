#include <cstddef>
#include <stdexcept>
#include <sys/mman.h>
#include <numaif.h>
#include <numa.h>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <iostream>
#include <errno.h>

#include "numa_memory_resource_no_jemalloc.hpp"

#define USE_MBIND // vs. numa_move_pages
inline std::size_t get_page_size()
{
    return static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
}

constexpr size_t HUGE_PAGE_SIZE = 2 * 1024 * 1024;
static const auto ACTUAL_PAGE_SIZE = get_page_size();

size_t align_to_huge_page_size(size_t size);

NumaMemoryResourceNoJemalloc::NumaMemoryResourceNoJemalloc(NodeID alloc_on_node, bool use_explicit_huge_pages, bool madvise_huge_pages) : _alloc_on_node(alloc_on_node), _use_explicit_huge_pages(use_explicit_huge_pages), _madvise_huge_pages(madvise_huge_pages)
{
    if (_use_explicit_huge_pages && _madvise_huge_pages)
    {
        throw std::logic_error("Choose either transparent or explicit huge pages or none.");
    }
};

NumaMemoryResourceNoJemalloc::~NumaMemoryResourceNoJemalloc()
{
}

void NumaMemoryResourceNoJemalloc::move_pages_policed(void *p, size_t size)
{
    const auto max_node = numa_max_node();
    if (max_node == 0)
    {
        return;
    }
    auto bitmask = numa_bitmask_alloc(max_node + 1);
    numa_bitmask_clearall(bitmask);
    numa_bitmask_setbit(bitmask, _alloc_on_node);
    auto ret = mbind(reinterpret_cast<char *>(p), size, MPOL_BIND, bitmask->maskp, bitmask->size + 1, 0);
    if (ret != 0)
    {
        throw std::runtime_error("mbind failed with " + std::to_string(ret) + " errno: " + strerror(errno));
    }
    numa_bitmask_clearbit(bitmask, _alloc_on_node);
    numa_bitmask_free(bitmask);
}

void *NumaMemoryResourceNoJemalloc::do_allocate(std::size_t bytes, std::size_t alignment)
{
    // map return addresses aligned to page size
#ifdef USE_MBIND
    auto mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS;
#else
    auto mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE;
#endif
    if (_use_explicit_huge_pages)
    {
        mmap_flags |= MAP_HUGETLB;
    }
    if (_use_explicit_huge_pages || _madvise_huge_pages)
    {
        bytes = align_to_huge_page_size(bytes);
    }

    void *addr = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, mmap_flags, -1, 0);
    if (addr == nullptr || addr == MAP_FAILED)
    {
        throw std::runtime_error("Failed to mmap pages. errno: " + std::to_string(errno) + " err: " + std::string{strerror(errno)});
    }

    move_pages_policed(addr, bytes);

    if (_madvise_huge_pages)
    {
        if (int ret = madvise(addr, bytes, MADV_HUGEPAGE) != 0)
        {
            throw std::runtime_error("madvise failed. Ernno : " + std::to_string(ret));
        }
    }
    return addr;
}

void NumaMemoryResourceNoJemalloc::do_deallocate(void *p, std::size_t bytes, std::size_t alignment)
{
    if (_use_explicit_huge_pages || _madvise_huge_pages)
    {
        bytes = align_to_huge_page_size(bytes);
    }

    auto ret = munmap(p, bytes);
    if (ret == -1)
    {
        std::cerr << "munmap failed: " << strerror(errno) << std::endl;
        // dont exit here, even though we probably should :)
    }
}

bool NumaMemoryResourceNoJemalloc::do_is_equal(const memory_resource &other) const noexcept
{
    return &other == this;
}
