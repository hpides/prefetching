#pragma once

#include <mutex>
#include <vector>
#include <memory_resource>
#include <stdexcept>
#include <memory_resource>

#include "../types.hpp"


// Only applicable in very limited use cases -> deallocation behavior is not
// implemented nor intended, thus memory leaks will occur.
class SimpleContinuousAllocator : public std::pmr::memory_resource
{
public:
    SimpleContinuousAllocator(std::pmr::memory_resource &allocator, size_t region_size, size_t region_alignment = alignof(std::max_align_t), bool zero_memory = false);

    ~SimpleContinuousAllocator();

    void *allocate(size_t size, size_t alignment = alignof(std::max_align_t));

    // "clears" all already allocated regions, so that new allocations can be placed on that memory, pretending its new memory.
    void clear_all_allocated_regions();

    // Methods defined by memory_resource.
    void *do_allocate(std::size_t bytes, std::size_t alignment) override;

    void do_deallocate(void *p, std::size_t bytes, std::size_t alignment) override;

    bool do_is_equal(const memory_resource &other) const noexcept override;

private:
    void set_new_region();
    void *allocate_impl(size_t size, size_t alignment = alignof(std::max_align_t));

    void *curr_region;
    size_t region_size;
    size_t region_alignment;
    size_t allocated;
    std::vector<void *> used_regions;
    std::vector<void *> clean_regions;
    bool zero_memory;
    std::pmr::memory_resource &allocator;

    std::mutex allocator_mutex;
};