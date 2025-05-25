#include "simple_continuous_allocator.hpp"
#include <cstring>

SimpleContinuousAllocator::SimpleContinuousAllocator(std::pmr::memory_resource &allocator, size_t region_size, size_t region_alignment, bool zero_memory) : allocator(allocator), region_size(region_size), region_alignment(region_alignment), zero_memory(zero_memory)
{
    std::lock_guard<std::mutex> lock(allocator_mutex);
    set_new_region();
}

void *SimpleContinuousAllocator::allocate(size_t size, size_t alignment)
{

    std::lock_guard<std::mutex> lock(allocator_mutex);

    return allocate_impl(size, alignment);
}

void *SimpleContinuousAllocator::allocate_impl(size_t size, size_t alignment)
{
    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
    {
        throw std::invalid_argument("Alignment must be a power of two");
    }

    uintptr_t curr_address = reinterpret_cast<uintptr_t>(curr_region) + allocated;
    uintptr_t aligned_address = (curr_address + (alignment - 1)) & ~(alignment - 1); // Align to the nearest boundary

    size_t padding = aligned_address - curr_address;

    if (size + padding <= (region_size - allocated))
    {
        allocated += size + padding;
        return reinterpret_cast<void *>(aligned_address);
    }
    else
    {
        if (size + alignment > region_size)
        {
            throw std::runtime_error("SimpleContinuousAllocator's region size is smaller than requested size (" + std::to_string(size) + ")");
        }
        set_new_region();
        return allocate_impl(size, alignment);
    }
}

void SimpleContinuousAllocator::set_new_region()
{
    // we don't need to lock here as set_new_region is only called from allocate, clear_all_allocated_regions, and in the constructor.
    if (clean_regions.empty())
    {
        curr_region = allocator.allocate(region_size, region_alignment);
    }
    else
    {
        curr_region = clean_regions.back();
        clean_regions.pop_back();
    }
    used_regions.push_back(curr_region);
    allocated = 0;
    if (zero_memory)
    {
        memset(curr_region, 0, region_size);
    }
}

void SimpleContinuousAllocator::clear_all_allocated_regions()
{
    std::lock_guard<std::mutex> lock(allocator_mutex);
    clean_regions.reserve(clean_regions.size() + used_regions.size());
    for (const auto region : used_regions)
    {
        clean_regions.push_back(region);
    }
    used_regions.clear();
    set_new_region();
}

void *SimpleContinuousAllocator::do_allocate(std::size_t bytes, std::size_t alignment)
{
    return this->allocate(bytes, alignment);
}

void SimpleContinuousAllocator::do_deallocate(void *p, std::size_t bytes, std::size_t alignment)
{
    // ignore deallocations
}

bool SimpleContinuousAllocator::do_is_equal(const memory_resource &other) const noexcept
{
    return &other == this;
}

SimpleContinuousAllocator::~SimpleContinuousAllocator()
{
    std::lock_guard<std::mutex> lock(allocator_mutex);

    for (const auto region : used_regions)
    {
        allocator.deallocate(region, region_size, region_alignment);
    }
    for (const auto region : clean_regions)
    {
        allocator.deallocate(region, region_size, region_alignment);
    }
}