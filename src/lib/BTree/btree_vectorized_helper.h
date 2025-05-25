#pragma once

#include <functional>
#include "coro.h"

template <typename, typename = std::void_t<>>
struct has_task_type : std::false_type
{
};

template <typename BTree>
struct has_task_type<BTree, std::void_t<typename BTree::task_type>> : std::true_type
{
};

template <typename, typename = std::void_t<>>
struct has_optimized_task_type : std::false_type
{
};
template <typename BTree>
struct has_optimized_task_type<BTree, std::void_t<typename BTree::optimized_task_type>> : std::true_type
{
};

template <typename BTree>
void co_insert(size_t from, size_t to, size_t num_coroutines, BTree &btree, auto &kv_pairs)
{
    for (unsigned i = from; i < to; ++i)
    {
        auto insert_task = btree.insert(kv_pairs[i].first, kv_pairs[i].second);
        while (!insert_task.is_done())
        {
            insert_task.resume();
        }
        insert_task.destroy();
    }
}

template <typename BTree>
void co_insert_optimized(size_t from, size_t to, size_t num_coroutines, BTree &btree, auto &kv_pairs)
{
    for (unsigned i = from; i < to; ++i)
    {
        throttler t(1);

        t.spawn(btree.insert(kv_pairs[i].first, kv_pairs[i].second));

        t.run();
    }
}

template <typename BTree>
void vec_insert(size_t from, size_t to, BTree &btree, auto &kv_pairs)
{
    for (unsigned i = from; i < to; ++i)
    {
        btree.insert(kv_pairs[i].first, kv_pairs[i].second);
    }
}

template <typename BTree>
void schedule_coroutines(size_t from, size_t to, size_t num_coroutines, BTree &btree, auto &kv_pairs)
{
    const size_t lookups_per_thread = to - from;
    // TAKEN FROM "LOOKING UNDER THE HOOD OF"
    std::uint64_t parallel_coroutines = std::min<std::uint64_t>(num_coroutines, lookups_per_thread);

    /// Space to store values for lookups.
    auto values = std::vector<std::uint64_t>{};
    auto actuals = std::vector<std::uint64_t>{};
    values.resize(parallel_coroutines);
    actuals.resize(parallel_coroutines);

    /// Coroutines that await execution.
    auto active_coroutine_frames = std::vector<typename BTree::task_type>{};

    auto request_index = 0U;

    /// Store the first coroutines within the active frame.
    for (auto i = 0U; i < parallel_coroutines; ++i)
    {
        const auto &request = kv_pairs[request_index++];
        active_coroutine_frames.push_back(btree.lookup(request.first, values[i]));
        actuals[i] = request.second;
    }

    /// Dispatch coroutines until all requests are done AND all coroutines finished.
    std::uint32_t count_finished_coroutine_frames;
    do
    {
        count_finished_coroutine_frames = 0U;
        for (auto i = 0U; i < parallel_coroutines; ++i)
        {
            if (!active_coroutine_frames[i].is_done())
            {
                active_coroutine_frames[i].resume();
            }
            else
            {
                if (actuals[i] != values[i])
                {
                    throw std::runtime_error("Wrong value returned. got: " + std::to_string(values[i]) + " expected: " + std::to_string(actuals[i]));
                }
                if (request_index < lookups_per_thread)
                {
                    /// Free the coro frame.
                    active_coroutine_frames[i].destroy();

                    /// If the coroutine was finished, create a new one for the next request---if any.
                    const auto &request = kv_pairs[request_index++];
                    active_coroutine_frames[i] = btree.lookup(request.first, values[i]);
                    actuals[i] = request.second;
                }
                else /// Otherwise, only wait to finish the last requests.
                {
                    ++count_finished_coroutine_frames;
                }
            }
        }
    } while (count_finished_coroutine_frames < parallel_coroutines);

    // Cleanup
    for (auto i = 0U; i < parallel_coroutines; ++i)
    {
        active_coroutine_frames[i].destroy();
    };
}

template <typename BTree>
void schedule_coroutines_optimized(size_t from, size_t to, size_t num_coroutines, BTree &btree, auto &kv_pairs)
{
    const size_t lookups_per_thread = to - from;
    std::uint64_t parallel_coroutines = std::min<std::uint64_t>(num_coroutines, lookups_per_thread);

    auto values = std::vector<std::uint64_t>{};
    values.resize(lookups_per_thread);

    throttler t(parallel_coroutines);

    // for (auto [key, value] : lookups)
    for (unsigned i = from; i < to; i++)
        t.spawn(btree.lookup(kv_pairs[i].first, values[i - from]));

    t.run();
    for (unsigned i = from; i < to; i++)
    {
        if (values[i - from] != kv_pairs[i].second)
        {
            throw std::runtime_error("Wrong value encountered in lookup. got " + std::to_string(values[i - from]) + " expected: " + std::to_string(kv_pairs[i].second));
        }
    }
}

template <typename BTree>
void vectorized_get(size_t from, size_t to, BTree &btree, auto &kv_pairs)
{
    for (unsigned i = from; i < to; ++i)
    {
        std::uint64_t res = 0;
        btree.lookup(kv_pairs[i].first, res);
        if (res != kv_pairs[i].second)
        {
            throw std::runtime_error("Btree wrong element got: " + std::to_string(res) + " expected: " + std::to_string(kv_pairs[i].second));
        }
    }
}