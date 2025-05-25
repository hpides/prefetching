#pragma once

#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <sched.h>
#include "builtin.h"

#include "../utils/simple_continuous_allocator.hpp"

namespace btreeolc
{

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

    template <const uint64_t pageSize>
    struct NodeBase : public OptLock
    {
        PageType type;
        uint16_t count;

        virtual ~NodeBase() = default;
    };

    template <const uint64_t pageSize>
    struct BTreeLeafBase : public NodeBase<pageSize>
    {
        static const PageType typeMarker = PageType::BTreeLeaf;

        virtual ~BTreeLeafBase() = default;
    };

    template <class Key, class Payload, const uint64_t pageSize>
    struct BTreeLeaf : public BTreeLeafBase<pageSize>
    {
        struct Entry
        {
            Key k;
            Payload p;
        };

        static const uint64_t maxEntries = (pageSize - sizeof(NodeBase<pageSize>)) / (sizeof(Key) + sizeof(Payload));

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

        BTreeLeaf<Key, Payload, pageSize> *split(Key &sep, SimpleContinuousAllocator &allocator)
        {
            void *new_leaf_memory = allocator.allocate(sizeof(BTreeLeaf<Key, Payload, pageSize>), alignof(BTreeLeaf<Key, Payload, pageSize>));
            BTreeLeaf<Key, Payload, pageSize> *newLeaf = new (new_leaf_memory) BTreeLeaf<Key, Payload, pageSize>();
            newLeaf->count = this->count - (this->count / 2);
            this->count = this->count - newLeaf->count;
            memcpy(newLeaf->keys, keys + this->count, sizeof(Key) * newLeaf->count);
            memcpy(newLeaf->payloads, payloads + this->count, sizeof(Payload) * newLeaf->count);
            sep = keys[this->count - 1];
            return newLeaf;
        }
    };

    template <const uint64_t pageSize>
    struct BTreeInnerBase : public NodeBase<pageSize>
    {
        static const PageType typeMarker = PageType::BTreeInner;

        virtual ~BTreeInnerBase() = default;
    };

    template <class Key, const uint64_t pageSize>
    struct BTreeInner : public BTreeInnerBase<pageSize>
    {
        static const uint64_t maxEntries = (pageSize - sizeof(NodeBase<pageSize>)) / (sizeof(Key) + sizeof(NodeBase<pageSize> *));
        NodeBase<pageSize> *children[maxEntries];
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

        BTreeInner<Key, pageSize> *split(Key &sep, SimpleContinuousAllocator &allocator)
        {
            void *new_inner_memory = allocator.allocate(sizeof(BTreeInner<Key, pageSize>), alignof(BTreeInner<Key, pageSize>));
            BTreeInner<Key, pageSize> *newInner = new (new_inner_memory) BTreeInner<Key, pageSize>();
            newInner->count = this->count - (this->count / 2);
            this->count = this->count - newInner->count - 1;
            sep = keys[this->count];
            memcpy(newInner->keys, keys + this->count + 1, sizeof(Key) * (newInner->count + 1));
            memcpy(newInner->children, children + this->count + 1, sizeof(NodeBase<pageSize> *) * (newInner->count + 1));
            return newInner;
        }

        void insert(Key k, NodeBase<pageSize> *child)
        {
            assert(this->count < maxEntries - 1);
            unsigned pos = lowerBound(k);
            memmove(keys + pos + 1, keys + pos, sizeof(Key) * (this->count - pos + 1));
            memmove(children + pos + 1, children + pos, sizeof(NodeBase<pageSize> *) * (this->count - pos + 1));
            keys[pos] = k;
            children[pos] = child;
            std::swap(children[pos], children[pos + 1]);
            this->count++;
        }
    };

    template <class Key, class Value, const uint64_t pageSize>
    struct BTree
    {
        std::atomic<NodeBase<pageSize> *> root;
        SimpleContinuousAllocator &allocator;

        BTree(SimpleContinuousAllocator &allocator) : allocator(allocator)
        {
            void *new_leaf_memory = allocator.allocate(sizeof(BTreeLeaf<Key, Value, pageSize>), alignof(BTreeLeaf<Key, Value, pageSize>));
            root = new (new_leaf_memory) BTreeLeaf<Key, Value, pageSize>();
        }

        ~BTree() { root.load()->~NodeBase(); }

        void makeRoot(Key k, NodeBase<pageSize> *leftChild, NodeBase<pageSize> *rightChild)
        {
            void *new_inner_memory = allocator.allocate(sizeof(BTreeInner<Key, pageSize>), alignof(BTreeInner<Key, pageSize>));
            auto inner = new (new_inner_memory) BTreeInner<Key, pageSize>();
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

        void insert(Key k, Value v)
        {
            int restartCount = 0;
        restart:
            if (restartCount++)
                yield(restartCount);
            bool needRestart = false;

            // Current node
            NodeBase<pageSize> *node = root;
            uint64_t versionNode = node->readLockOrRestart(needRestart);
            if (needRestart || (node != root))
                goto restart;

            // Parent of current node
            BTreeInner<Key, pageSize> *parent = nullptr;
            uint64_t versionParent;

            while (node->type == PageType::BTreeInner)
            {
                auto inner = static_cast<BTreeInner<Key, pageSize> *>(node);

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
                    BTreeInner<Key, pageSize> *newInner = inner->split(sep, allocator);
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

            auto leaf = static_cast<BTreeLeaf<Key, Value, pageSize> *>(node);

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
                BTreeLeaf<Key, Value, pageSize> *newLeaf = leaf->split(sep, allocator);
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
                return; // success
            }
        }

        bool lookup(Key k, Value &result)
        {
            int restartCount = 0;
        restart:
            if (restartCount++)
                yield(restartCount);
            bool needRestart = false;

            NodeBase<pageSize> *node = root;
            uint64_t versionNode = node->readLockOrRestart(needRestart);
            if (needRestart || (node != root))
                goto restart;

            // Parent of current node
            BTreeInner<Key, pageSize> *parent = nullptr;
            uint64_t versionParent;

            while (node->type == PageType::BTreeInner)
            {
                auto inner = static_cast<BTreeInner<Key, pageSize> *>(node);

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

            BTreeLeaf<Key, Value, pageSize> *leaf = static_cast<BTreeLeaf<Key, Value, pageSize> *>(node);
            unsigned pos = leaf->lowerBound(k);
            bool success;
            if ((pos < leaf->count) && (leaf->keys[pos] == k))
            {
                success = true;
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

            return success;
        }

        uint64_t scan(Key k, int range, Value *output)
        {
            int restartCount = 0;
        restart:
            if (restartCount++)
                yield(restartCount);
            bool needRestart = false;

            NodeBase<pageSize> *node = root;
            uint64_t versionNode = node->readLockOrRestart(needRestart);
            if (needRestart || (node != root))
                goto restart;

            // Parent of current node
            BTreeInner<Key, pageSize> *parent = nullptr;
            uint64_t versionParent;

            while (node->type == PageType::BTreeInner)
            {
                auto inner = static_cast<BTreeInner<Key, pageSize> *>(node);

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

            BTreeLeaf<Key, Value, pageSize> *leaf = static_cast<BTreeLeaf<Key, Value, pageSize> *>(node);
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
