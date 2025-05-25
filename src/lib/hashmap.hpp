#include <iostream>
#include <list>
#include <vector>
#include <coroutine>
#include <iterator>
#include <assert.h>

#include "coroutine.hpp"
#include "utils/profiler.cpp"
#include "numa/numa_memory_resource.hpp"

using namespace std;

template <typename K, typename V>
class Node
{
public:
    K key;
    V value;
};

template<typename K, typename V>
class HashMap {
private:
    std::pmr::vector<std::pmr::list<Node<K, V>>> table;
    size_t size;
    size_t capacity;
    std::pmr::memory_resource &memory_resource;

    size_t hash(const K& key);

    struct AMAC_state {
        K key;
        typename std::list<Node<K, V>>::iterator node;
        typename std::list<Node<K, V>>::iterator end;
        int stage = 0;
        int i;
    };

public:
    PrefetchProfiler &profiler;

    HashMap(size_t capacity, PrefetchProfiler &profiler, std::pmr::memory_resource &memory_resource);
    ~HashMap();
    void insert(const K& key, const V& value);
    V& get(const K& key);
    coroutine get_co(const K& key, std::vector<V>& results, int i);
    coroutine get_co_exp(const K &key, std::vector<V> &results, int i);
    coroutine profile_get_co_exp(const K &key, std::vector<V> &results, int i);
    void vectorized_get(const std::vector<K>& keys, std::vector<V>& results);
    void vectorized_get_gp(const std::vector<K>& keys, std::vector<V>& results);
    void vectorized_get_amac(const std::vector<K>& keys, std::vector<V>& results, int group_size);
    void vectorized_get_coroutine(const std::vector<K>& keys, std::vector<V>& results, int group_size);
    void vectorized_get_coroutine_exp(const std::vector<K> &keys, std::vector<V> &results, int group_size);
    void profile_vectorized_get_coroutine_exp(const std::vector<K> &keys, std::vector<V> &results, int group_size);
    void remove(const K& key);
    bool contains(const K& key);
    size_t getSize() const;
    bool isEmpty() const;
};
