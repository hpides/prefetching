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

#include <boost/container/pmr/memory_resource.hpp>
#include <jemalloc/jemalloc.h>

#include "numa_memory_resource.hpp"

#define USE_MBIND // vs. numa_move_pages

std::unordered_map<unsigned, NumaMemoryResource *> arena_to_resource_map;

static const auto ACTUAL_PAGE_SIZE = get_page_size();

std::size_t calculate_allocated_pages(size_t size)
{
    return (size + ACTUAL_PAGE_SIZE - 1) / ACTUAL_PAGE_SIZE;
}

NumaMemoryResource::NumaMemoryResource(bool use_explicit_huge_pages, bool madvise_huge_pages) : _use_explicit_huge_pages(use_explicit_huge_pages), _madvise_huge_pages(madvise_huge_pages)
{
    if (_use_explicit_huge_pages && _madvise_huge_pages)
    {
        throw std::logic_error("Choose either transparent or explicit huge pages or none.");
    }
    arena_id = uint32_t{0};
    auto size = sizeof(arena_id);
    if (mallctl("arenas.create", static_cast<void *>(&arena_id), &size, nullptr, 0) != 0)
    {
        throw std::runtime_error("Could not create arena");
    }

    std::ostringstream hooks_key;
    hooks_key << "arena." << arena_id << ".extent_hooks";
    extent_hooks_t *hooks;
    size = sizeof(hooks);
    // Read the existing hooks
    if (auto ret = mallctl(hooks_key.str().c_str(), &hooks, &size, nullptr, 0))
    {
        throw std::runtime_error("Unable to get the hooks");
    }

    // Set the custom hook
    extentHooks_ = *hooks;
    extentHooks_.alloc = &alloc;
    extentHooks_.dalloc = &dalloc;
    extentHooks_.commit = hooks->commit;
    extentHooks_.decommit = hooks->decommit;
    extentHooks_.destroy = hooks->destroy;
    extentHooks_.merge = hooks->merge;
    extentHooks_.purge_forced = hooks->purge_forced;
    extentHooks_.purge_lazy = hooks->purge_lazy;
    extentHooks_.split = hooks->split;
    extent_hooks_t *new_hooks = &extentHooks_;
    if (auto ret = mallctl(
            hooks_key.str().c_str(),
            nullptr,
            nullptr,
            &new_hooks,
            sizeof(new_hooks)))
    {
        throw std::runtime_error("Unable to set the hooks");
    }

    _allocation_flags = MALLOCX_ARENA(arena_id) | MALLOCX_TCACHE_NONE;
    arena_to_resource_map[arena_id] = this;
};

NumaMemoryResource::~NumaMemoryResource()
{
    std::ostringstream delete_key;
    delete_key << "arena." << arena_id << ".destroy";

    if (auto ret = mallctl(delete_key.str().c_str(), nullptr, nullptr, nullptr, 0))
    {
        std::cerr << "Unable to destroy the arena: " << ret << std::endl;
    }

    arena_to_resource_map.erase(arena_id);
}

void *NumaMemoryResource::do_allocate(std::size_t bytes, std::size_t alignment)
{
    const auto addr = mallocx(bytes, _allocation_flags | MALLOCX_ALIGN(alignment));
    return addr;
}

void NumaMemoryResource::do_deallocate(void *p, std::size_t bytes, std::size_t alignment)
{
    dallocx(p, _allocation_flags);
}

bool NumaMemoryResource::do_is_equal(const memory_resource &other) const noexcept
{
    return &other == this;
}

NodeID NumaMemoryResource::node_id(void *p)
{
    return 0;
}

size_t align_to_huge_page_size(size_t size)
{
    return (size + HUGE_PAGE_SIZE - 1) & ~(HUGE_PAGE_SIZE - 1);
}

void *NumaMemoryResource::alloc(extent_hooks_t *extent_hooks, void *new_addr, size_t size, size_t alignment, bool *zero,
                                bool *commit, unsigned arena_index)
{
    // map return addresses aligned to page size
#ifdef USE_MBIND
    auto mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS;
#else
    auto mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE;
#endif
    auto memory_resource = arena_to_resource_map[arena_index];
    if (memory_resource->_use_explicit_huge_pages)
    {
        mmap_flags |= MAP_HUGETLB;
    }
    if (memory_resource->_use_explicit_huge_pages || memory_resource->_madvise_huge_pages)
    {
        // TODO: Somehow tell jemalloc to use at least 2MB large allocations.
        size = align_to_huge_page_size(size);
    }

    void *addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, mmap_flags, -1, 0);
    if (addr == nullptr || addr == MAP_FAILED)
    {
        throw std::runtime_error("Failed to mmap pages. errno: " + std::to_string(errno) + " err: " + std::string{strerror(errno)});
    }
    unsigned long num_pages = calculate_allocated_pages(size);

    memory_resource->move_pages_policed(addr, size);

    if (memory_resource->_madvise_huge_pages)
    {
        if (int ret = madvise(addr, size, MADV_HUGEPAGE) != 0)
        {
            throw std::runtime_error("madvise failed. Ernno : " + std::to_string(ret));
        }
    }
    *commit = true;
    return addr;
}

bool NumaMemoryResource::dalloc(extent_hooks_t *extent_hooks, void *addr, size_t size, bool committed, unsigned arena_ind)
{

    if (arena_to_resource_map[arena_ind]->_use_explicit_huge_pages || arena_to_resource_map[arena_ind]->_madvise_huge_pages)
    {
        size = align_to_huge_page_size(size);
    }

    auto ret = munmap(addr, size);
    if (ret == -1)
    {
        std::cerr << "munmap failed: " << strerror(errno) << std::endl;
        return false;
    }
    else
    {
        return true;
    }
}