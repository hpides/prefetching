#include <vector>
#include <coroutine>

#include "random_access.hpp"
#include "coroutine.hpp"
#include "utils.cpp"
#include "types.hpp"

/*
    As there are no computations between the data accesses, we can no longer
    really hide direct access latencies behind computations. Runtimes should be
    dependent on the overhead imposed by the different methods (e.g., coroutine
    creation, state management, switch overheads, ...).
*/

template <typename V>
RandomAccess<V>::RandomAccess(size_t num_elements)
    : num_elements(num_elements)
{
    data = new V[num_elements];
    for (size_t i = 0; i < num_elements; ++i)
    {
        data[i] = i;
    }
}

template <typename V>
V &RandomAccess<V>::get(size_t pos)
{
    return data[pos];
}

template <typename V>
coroutine RandomAccess<V>::get_co(size_t pos, std::vector<V> &results, int i)
{
    __builtin_prefetch(data + pos, 0, 3);
    co_await std::suspend_always{};
    results[i] = data[pos];
    co_return;
}

template <typename V>
coroutine RandomAccess<V>::get_co_exp(size_t pos, std::vector<V> &results, int i)
{
    if (is_in_tlb_and_prefetch(data + pos))
    {
        co_await std::suspend_always{};
    }
    results[i] = data[pos];
    co_return;
}

template <typename V>
void RandomAccess<V>::vectorized_get(const std::vector<size_t> &positions, std::vector<V> &results)
{
    for (size_t i = 0; i < positions.size(); ++i)
    {
        results.at(i) = data[positions[i]];
    }
}

template <typename V>
void RandomAccess<V>::vectorized_get_gp(const std::vector<size_t> &positions, std::vector<V> &results)
{
    // Normally we would need to track states to handle more sophisticated
    // function logic. Here this is not required.
    for (auto pos : positions)
    {
        __builtin_prefetch(data + pos, 0, 3);
    }
    for (size_t i = 0; i < positions.size(); ++i)
    {
        results.at(i) = data[positions[i]];
    }
}

template <typename V>
void RandomAccess<V>::vectorized_get_amac(const std::vector<size_t> &positions, std::vector<V> &results, size_t group_size)
{
    // Normally we would need to track states to handle more sophisticated
    // function logic. Here, this is not required.
    for (size_t group_iteration = 0; group_iteration * group_size < positions.size(); ++group_iteration)
    {
        size_t group_offset = group_iteration * group_size;
        for (size_t i = 0; i < std::min(group_size, positions.size() - group_offset); ++i)
        {
            __builtin_prefetch(data + positions[group_offset + i], 0, 3);
        }
        for (size_t i = 0; i < std::min(group_size, positions.size() - group_offset); ++i)
        {
            results.at(group_offset + i) = data[positions[group_offset + i]];
        }
    }
}

template <typename V>
void RandomAccess<V>::vectorized_get_coroutine(const std::vector<size_t> &positions, std::vector<V> &results, size_t group_size)
{
    CircularBuffer<std::coroutine_handle<promise>> buff(std::min(group_size, static_cast<size_t>(positions.size())));

    int num_finished = 0;
    int i = 0;

    while (num_finished < positions.size())
    {
        std::coroutine_handle<promise> &handle = buff.next_state();
        if (!handle)
        {
            if (i < std::min(group_size, static_cast<size_t>(positions.size())))
            {
                handle = get_co(positions[i], results, i);
                i++;
            }
            continue;
        }

        if (handle.done())
        {
            num_finished++;
            handle.destroy();
            if (i < positions.size())
            {
                handle = get_co(positions[i], results, i);
                ++i;
            }
            else
            {
                handle = nullptr;
                continue;
            }
        }

        handle.resume();
    }
}

template <typename V>
void RandomAccess<V>::vectorized_get_coroutine_exp(const std::vector<size_t> &positions, std::vector<V> &results, size_t group_size)
{
    CircularBuffer<std::coroutine_handle<promise>> buff(std::min(group_size, static_cast<size_t>(positions.size())));

    int num_finished = 0;
    int i = 0;

    while (num_finished < positions.size())
    {
        std::coroutine_handle<promise> &handle = buff.next_state();
        if (!handle)
        {
            if (i < std::min(group_size, static_cast<size_t>(positions.size())))
            {
                handle = get_co_exp(positions[i], results, i);
                i++;
            }
            continue;
        }

        if (handle.done())
        {
            num_finished++;
            handle.destroy();
            if (i < positions.size())
            {
                handle = get_co_exp(positions[i], results, i);
                ++i;
            }
            else
            {
                handle = nullptr;
                continue;
            }
        }

        handle.resume();
    }
}

template <typename V>
size_t RandomAccess<V>::getSize() const
{
    return num_elements;
}

template class RandomAccess<uint8_t>;
template class RandomAccess<uint16_t>;
template class RandomAccess<uint32_t>;
template class RandomAccess<uint64_t>;
