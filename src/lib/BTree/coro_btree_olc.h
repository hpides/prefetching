#pragma once

#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <sched.h>
#include <coroutine>
#include "prefetch.h"
#include "builtin.h"

#include "../utils/simple_continuous_allocator.hpp"

/***
 * Layout of the BtreeOLC
 *
 * ### Inner nodes ###
 *    What      From       To       Cache Lines
 *   -------------------------------------------
 *    Base      0          23       0
 *    Children  24         519      0-8
 *    Keys      520        1015     8-15
 *
 * ### Leaf nodes ###
 *    What      From       To       Cache Lines
 *   -------------------------------------------
 *    Base      0          23       0
 *    Keys      24         519      0-8
 *    Values    520        1015     8-15
 */

namespace btreeolc::coro {

class Task
{
public:
    // The coroutine level type
    struct promise_type
    {
        using Handle = std::coroutine_handle<promise_type>;
        Task get_return_object() { return Task{Handle::from_promise(*this)}; }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}

    };

    explicit Task(promise_type::Handle coroutine) : _coroutine(coroutine) {}
    void destroy() { _coroutine.destroy(); }
    void resume() { _coroutine.resume(); }

    [[nodiscard]] bool is_done() const { return _coroutine.done(); }

private:
    promise_type::Handle _coroutine;
};

enum class PageType : uint8_t
{
    BTreeInner = 1,
    BTreeLeaf = 2
};

struct OptLock
{
    std::atomic<uint64_t> typeVersionLockObsolete{0b100};

    bool isLocked(uint64_t version) { return ((version & 0b10) == 0b10); }

    uint64_t readLockOrRestart(bool &needRestart)
    {
        uint64_t version;
        version = typeVersionLockObsolete.load();
        if (isLocked(version) || isObsolete(version))
        {
            builtin::pause();
            needRestart = true;
        }
        return version;
    }

    void writeLockOrRestart(bool &needRestart)
    {
        uint64_t version;
        version = readLockOrRestart(needRestart);
        if (needRestart)
            return;

        upgradeToWriteLockOrRestart(version, needRestart);
        if (needRestart)
            return;
    }

    void upgradeToWriteLockOrRestart(uint64_t &version, bool &needRestart)
    {
        if (typeVersionLockObsolete.compare_exchange_strong(version, version + 0b10))
        {
            version = version + 0b10;
        }
        else
        {
            builtin::pause();
            needRestart = true;
        }
    }

    void writeUnlock() { typeVersionLockObsolete.fetch_add(0b10); }

    bool isObsolete(uint64_t version) { return (version & 1) == 1; }

    void checkOrRestart(uint64_t startRead, bool &needRestart) const { readUnlockOrRestart(startRead, needRestart); }

    void readUnlockOrRestart(uint64_t startRead, bool &needRestart) const
    {
        needRestart = (startRead != typeVersionLockObsolete.load());
    }

    void writeUnlockObsolete() { typeVersionLockObsolete.fetch_add(0b11); }
};

template <const uint64_t pageSize, const uint64_t cacheLineSize>
struct NodeBase : public OptLock
{
    PageType type;
    uint16_t count;

    void prefetch_header()
    {
        SWPrefetcher::prefetch<0U, 1U, SWPrefetcher::Target::ALL>(this);
    }

    void prefetch_full()
    {
        SWPrefetcher::prefetch<0U, pageSize / cacheLineSize, SWPrefetcher::Target::ALL>(this);
    }

    virtual ~NodeBase() = default;
};

template <const uint64_t pageSize, const uint64_t cacheLineSize>
struct BTreeLeafBase : public NodeBase<pageSize, cacheLineSize>
{
    static const PageType typeMarker = PageType::BTreeLeaf;

    virtual ~BTreeLeafBase() = default;
};

template <class Key, class Payload, const uint64_t pageSize, const uint64_t cacheLineSize>
struct BTreeLeaf : public BTreeLeafBase<pageSize, cacheLineSize>
{
    struct Entry
    {
        Key k;
        Payload p;
    };

    static const uint64_t maxEntries = (pageSize - sizeof(NodeBase<pageSize, cacheLineSize>)) / (sizeof(Key) + sizeof(Payload));

    Key keys[maxEntries];
    Payload payloads[maxEntries];

    BTreeLeaf()
    {
        this->count = 0;
        this->type = this->typeMarker;
    }

    virtual ~BTreeLeaf() = default;

    bool isFull() { return this->count == maxEntries; };

    unsigned lowerBound(Key k)
    {
        unsigned lower = 0;
        unsigned upper = this->count;
        do
        {
            unsigned mid = ((upper - lower) / 2) + lower;
            if (k < keys[mid])
            {
                upper = mid;
            }
            else if (k > keys[mid])
            {
                lower = mid + 1;
            }
            else
            {
                return mid;
            }
        } while (lower < upper);
        return lower;
    }

    unsigned lowerBoundBF(Key k)
    {
        auto base = keys;
        unsigned n = this->count;
        while (n > 1)
        {
            const unsigned half = n / 2;
            base = (base[half] < k) ? (base + half) : base;
            n -= half;
        }
        return (*base < k) + base - keys;
    }

    void insert(Key k, Payload p)
    {
        assert(this->count < maxEntries);
        if (this->count)
        {
            unsigned pos = lowerBound(k);
            if ((pos < this->count) && (keys[pos] == k))
            {
                // Upsert
                payloads[pos] = p;
                return;
            }
            memmove(keys + pos + 1, keys + pos, sizeof(Key) * (this->count - pos));
            memmove(payloads + pos + 1, payloads + pos, sizeof(Payload) * (this->count - pos));
            keys[pos] = k;
            payloads[pos] = p;
        }
        else
        {
            keys[0] = k;
            payloads[0] = p;
        }
        this->count++;
    }

    BTreeLeaf<Key, Payload, pageSize, cacheLineSize> *split(Key &sep, SimpleContinuousAllocator &allocator)
    {
        void *new_leaf_memory = allocator.allocate(sizeof(BTreeLeaf<Key, Payload, pageSize, cacheLineSize>), alignof(BTreeLeaf<Key, Payload, pageSize, cacheLineSize>));

        BTreeLeaf<Key, Payload, pageSize, cacheLineSize> *newLeaf = new (new_leaf_memory) BTreeLeaf<Key, Payload, pageSize, cacheLineSize>();
        newLeaf->count = this->count - (this->count / 2);
        this->count = this->count - newLeaf->count;
        memcpy(newLeaf->keys, keys + this->count, sizeof(Key) * newLeaf->count);
        memcpy(newLeaf->payloads, payloads + this->count, sizeof(Payload) * newLeaf->count);
        sep = keys[this->count - 1];
        return newLeaf;
    }

    void prefetch_keys()
    {
        SWPrefetcher::prefetch<0U, (pageSize / cacheLineSize) / 2, SWPrefetcher::Target::ALL>(this);
    }

    void prefetch_values()
    {
        SWPrefetcher::prefetch<(pageSize / cacheLineSize) / 2, (pageSize / cacheLineSize) / 2, SWPrefetcher::Target::ALL>(this);
    }

    void prefetch_value(const std::uint32_t pos)
    {
        SWPrefetcher::prefetch<0U, 1U, SWPrefetcher::Target::ALL>(&this->payloads[pos]);
    }
};

template <const uint64_t pageSize, const uint64_t cacheLineSize>
struct BTreeInnerBase : public NodeBase<pageSize, cacheLineSize>
{
    static const PageType typeMarker = PageType::BTreeInner;

    virtual ~BTreeInnerBase() = default;
};

template <class Key, const uint64_t pageSize, const uint64_t cacheLineSize>
struct BTreeInner : public BTreeInnerBase<pageSize, cacheLineSize>
{
    static const uint64_t maxEntries = (pageSize - sizeof(NodeBase<pageSize, cacheLineSize>)) / (sizeof(Key) + sizeof(NodeBase<pageSize, cacheLineSize> *));
    NodeBase<pageSize, cacheLineSize> *children[maxEntries];
    Key keys[maxEntries];

    BTreeInner()
    {
        this->count = 0;
        this->type = this->typeMarker;
    }

    virtual ~BTreeInner()
    {
        for (auto i = 0u; i <= this->count; i++)
        {
            if (children[i] != nullptr)
            {
                children[i]->~NodeBase();
            }
        }
    }

    bool isFull() { return this->count == (maxEntries - 1); };

    unsigned lowerBoundBF(Key k)
    {
        auto base = keys;
        unsigned n = this->count;
        while (n > 1)
        {
            const unsigned half = n / 2;
            base = (base[half] < k) ? (base + half) : base;
            n -= half;
        }
        return (*base < k) + base - keys;
    }

    unsigned lowerBound(Key k)
    {
        unsigned lower = 0;
        unsigned upper = this->count;
        do
        {
            unsigned mid = ((upper - lower) / 2) + lower;
            if (k < keys[mid])
            {
                upper = mid;
            }
            else if (k > keys[mid])
            {
                lower = mid + 1;
            }
            else
            {
                return mid;
            }
        } while (lower < upper);
        return lower;
    }

    BTreeInner<Key, pageSize, cacheLineSize> *split(Key &sep, SimpleContinuousAllocator &allocator)
    {
        void *new_inner_memory = allocator.allocate(sizeof(BTreeInner<Key, pageSize, cacheLineSize>), alignof(BTreeInner<Key, pageSize, cacheLineSize>));
        BTreeInner<Key, pageSize, cacheLineSize> *newInner = new (new_inner_memory) BTreeInner<Key, pageSize, cacheLineSize>();
        newInner->count = this->count - (this->count / 2);
        this->count = this->count - newInner->count - 1;
        sep = keys[this->count];
        memcpy(newInner->keys, keys + this->count + 1, sizeof(Key) * (newInner->count + 1));
        memcpy(newInner->children, children + this->count + 1, sizeof(NodeBase<pageSize, cacheLineSize> *) * (newInner->count + 1));
        return newInner;
    }

    void insert(Key k, NodeBase<pageSize, cacheLineSize> *child)
    {
        assert(this->count < maxEntries - 1);
        unsigned pos = lowerBound(k);
        memmove(keys + pos + 1, keys + pos, sizeof(Key) * (this->count - pos + 1));
        memmove(children + pos + 1, children + pos, sizeof(NodeBase<pageSize, cacheLineSize> *) * (this->count - pos + 1));
        keys[pos] = k;
        children[pos] = child;
        std::swap(children[pos], children[pos + 1]);
        this->count++;
    }

    void prefetch_children()
    {
        SWPrefetcher::prefetch<0U, (pageSize / cacheLineSize) / 2, SWPrefetcher::Target::ALL>(this);
    }

    void prefetch_keys()
    {
        SWPrefetcher::prefetch<(pageSize / cacheLineSize) / 2, (pageSize / cacheLineSize) / 2, SWPrefetcher::Target::ALL>(this);
    }
};

template <class Key, class Value, const uint64_t pageSize, const uint64_t cacheLineSize>
struct BTree
{
    using task_type = Task;

    std::atomic<NodeBase<pageSize, cacheLineSize> *> root;
    SimpleContinuousAllocator &allocator;

    BTree(SimpleContinuousAllocator &allocator) : allocator(allocator)
    {
        void *new_leaf_memory = allocator.allocate(sizeof(BTreeLeaf<Key, Value, pageSize, cacheLineSize>), alignof(BTreeLeaf<Key, Value, pageSize, cacheLineSize>));

        root = new (new_leaf_memory) BTreeLeaf<Key, Value, pageSize, cacheLineSize>();
    }

    ~BTree() { root.load()->~NodeBase(); }

    void makeRoot(Key k, NodeBase<pageSize, cacheLineSize> *leftChild, NodeBase<pageSize, cacheLineSize> *rightChild)
    {
        void *new_inner_memory = allocator.allocate(sizeof(BTreeInner<Key, pageSize, cacheLineSize>), alignof(BTreeInner<Key, pageSize, cacheLineSize>));

        auto inner = new (new_inner_memory) BTreeInner<Key, pageSize, cacheLineSize>();
        inner->count = 1;
        inner->keys[0] = k;
        inner->children[0] = leftChild;
        inner->children[1] = rightChild;
        root = inner;
    }

    void yield(int count)
    {
        if (count > 3)
            sched_yield();
        else
            builtin::pause();
    }

    Task insert(Key k, Value v)
    {
        int restartCount = 0;
    restart:
        if (restartCount++)
            yield(restartCount);
        bool needRestart = false;

        // Current node
        NodeBase<pageSize, cacheLineSize> *node = root;
        uint64_t versionNode = node->readLockOrRestart(needRestart);
        if (needRestart || (node != root))
            goto restart;

        // Parent of current node
        BTreeInner<Key, pageSize, cacheLineSize> *parent = nullptr;
        uint64_t versionParent;

        while (node->type == PageType::BTreeInner)
        {
            auto inner = static_cast<BTreeInner<Key, pageSize, cacheLineSize> *>(node);

            // Split eagerly if full
            if (inner->isFull())
            {
                // Lock
                if (parent)
                {
                    parent->upgradeToWriteLockOrRestart(versionParent, needRestart);
                    if (needRestart)
                        goto restart;
                }
                node->upgradeToWriteLockOrRestart(versionNode, needRestart);
                if (needRestart)
                {
                    if (parent)
                        parent->writeUnlock();
                    goto restart;
                }
                if (!parent && (node != root))
                { // there's a new parent
                    node->writeUnlock();
                    goto restart;
                }
                // Split
                Key sep;
                BTreeInner<Key, pageSize, cacheLineSize> *newInner = inner->split(sep, allocator);
                if (parent)
                    parent->insert(sep, newInner);
                else
                    makeRoot(sep, inner, newInner);
                // Unlock and restart
                node->writeUnlock();
                if (parent)
                    parent->writeUnlock();
                goto restart;
            }

            if (parent)
            {
                parent->readUnlockOrRestart(versionParent, needRestart);
                if (needRestart)
                    goto restart;
            }

            parent = inner;
            versionParent = versionNode;

            node = inner->children[inner->lowerBound(k)];
            inner->checkOrRestart(versionNode, needRestart);
            if (needRestart)
                goto restart;
            versionNode = node->readLockOrRestart(needRestart);
            if (needRestart)
                goto restart;
        }

        auto leaf = static_cast<BTreeLeaf<Key, Value, pageSize, cacheLineSize> *>(node);

        // Split leaf if full
        if (leaf->count == leaf->maxEntries)
        {
            // Lock
            if (parent)
            {
                parent->upgradeToWriteLockOrRestart(versionParent, needRestart);
                if (needRestart)
                    goto restart;
            }
            node->upgradeToWriteLockOrRestart(versionNode, needRestart);
            if (needRestart)
            {
                if (parent)
                    parent->writeUnlock();
                goto restart;
            }
            if (!parent && (node != root))
            { // there's a new parent
                node->writeUnlock();
                goto restart;
            }
            // Split
            Key sep;
            BTreeLeaf<Key, Value, pageSize, cacheLineSize> *newLeaf = leaf->split(sep, allocator);
            if (parent)
                parent->insert(sep, newLeaf);
            else
                makeRoot(sep, leaf, newLeaf);
            // Unlock and restart
            node->writeUnlock();
            if (parent)
                parent->writeUnlock();
            goto restart;
        }
        else
        {
            // only lock leaf node
            node->upgradeToWriteLockOrRestart(versionNode, needRestart);
            if (needRestart)
                goto restart;
            if (parent)
            {
                parent->readUnlockOrRestart(versionParent, needRestart);
                if (needRestart)
                {
                    node->writeUnlock();
                    goto restart;
                }
            }
            leaf->insert(k, v);
            node->writeUnlock();
            co_return; // success
        }
    }

    Task lookup(Key k, Value &result)
    {
        int restartCount = 0;
    restart:
        if (restartCount++)
            yield(restartCount);
        bool needRestart = false;

        NodeBase<pageSize, cacheLineSize> *node = root;
        uint64_t versionNode = node->readLockOrRestart(needRestart);
        if (needRestart || (node != root))
            goto restart;

        // Parent of current node
        BTreeInner<Key, pageSize, cacheLineSize> *parent = nullptr;
        uint64_t versionParent;

        while (node->type == PageType::BTreeInner)
        {
            auto inner = static_cast<BTreeInner<Key, pageSize, cacheLineSize> *>(node);

            if (parent)
            {
                parent->readUnlockOrRestart(versionParent, needRestart);
                if (needRestart)
                    goto restart;
            }

            parent = inner;
            versionParent = versionNode;

            /**
             * Accessing the keys of an inner node => Prefetch all keys
             */
            inner->prefetch_keys();
            co_await std::suspend_always{};
            const auto pos = inner->lowerBound(k);

            /*
             * TBD: We could also prefetch that specific cache line.
             */
            node = inner->children[pos];

            inner->checkOrRestart(versionNode, needRestart);
            if (needRestart)
                goto restart;

            /**
             * Accessing the header of a node => Prefetch only header
             */
            node->prefetch_header();
            co_await std::suspend_always{};
            versionNode = node->readLockOrRestart(needRestart);
            if (needRestart)
                goto restart;
        }

        BTreeLeaf<Key, Value, pageSize, cacheLineSize> *leaf = static_cast<BTreeLeaf<Key, Value, pageSize, cacheLineSize> *>(node);

        /**
         * Accessing the keys of a leaf node => Prefetch
         */
        leaf->prefetch_keys();
        co_await std::suspend_always{};
        unsigned pos = leaf->lowerBound(k);
        if ((pos < leaf->count) && (leaf->keys[pos] == k))
        {
            /**
             * Accessing the value of a leaf node => Prefetch that specific cache line.
             */
            leaf->prefetch_value(pos);
            co_await std::suspend_always{};
            result = leaf->payloads[pos];
        }
        if (parent)
        {
            parent->readUnlockOrRestart(versionParent, needRestart);
            if (needRestart)
                goto restart;
        }
        node->readUnlockOrRestart(versionNode, needRestart);
        if (needRestart)
            goto restart;

        co_return;
    }

    uint64_t scan(Key k, int range, Value *output)
    {
        int restartCount = 0;
    restart:
        if (restartCount++)
            yield(restartCount);
        bool needRestart = false;

        NodeBase<pageSize, cacheLineSize> *node = root;
        uint64_t versionNode = node->readLockOrRestart(needRestart);
        if (needRestart || (node != root))
            goto restart;

        // Parent of current node
        BTreeInner<Key, pageSize, cacheLineSize> *parent = nullptr;
        uint64_t versionParent;

        while (node->type == PageType::BTreeInner)
        {
            auto inner = static_cast<BTreeInner<Key, pageSize, cacheLineSize> *>(node);

            if (parent)
            {
                parent->readUnlockOrRestart(versionParent, needRestart);
                if (needRestart)
                    goto restart;
            }

            parent = inner;
            versionParent = versionNode;

            node = inner->children[inner->lowerBound(k)];
            inner->checkOrRestart(versionNode, needRestart);
            if (needRestart)
                goto restart;
            versionNode = node->readLockOrRestart(needRestart);
            if (needRestart)
                goto restart;
        }

        BTreeLeaf<Key, Value, pageSize, cacheLineSize> *leaf = static_cast<BTreeLeaf<Key, Value, pageSize, cacheLineSize> *>(node);
        unsigned pos = leaf->lowerBound(k);
        int count = 0;
        for (unsigned i = pos; i < leaf->count; i++)
        {
            if (count == range)
                break;
            output[count++] = leaf->payloads[i];
        }

        if (parent)
        {
            parent->readUnlockOrRestart(versionParent, needRestart);
            if (needRestart)
                goto restart;
        }
        node->readUnlockOrRestart(versionNode, needRestart);
        if (needRestart)
            goto restart;

        return count;
    }
};

} // namespace btreeolc
