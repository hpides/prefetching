#include "hashmap.hpp"
#include "utils.cpp"

template<typename K, typename V>
size_t HashMap<K, V>::hash(const K& key) {
    return std::hash<K>{}(key) % capacity;
}

template <typename K, typename V>
HashMap<K, V>::HashMap(size_t capacity, PrefetchProfiler &profiler, std::pmr::memory_resource &memory_resource) : capacity(capacity), size(0), profiler(profiler), memory_resource(memory_resource), table(&memory_resource)
{
    table.resize(capacity);
    for (int i = 0; i < capacity; ++i)
    {
        table.emplace_back(std::pmr::list<Node<K, V>>(&memory_resource));
    }
}

template<typename K, typename V>
HashMap<K, V>::~HashMap() {}

template<typename K, typename V>
void HashMap<K, V>::insert(const K& key, const V& value) {
    size_t index = hash(key);
    for (auto& node : table[index]) {
        if (node.key == key) {
            node.value = value;
            return;
        }
    }
    table[index].emplace_back(key, value);
    size++;
}

template<typename K, typename V>
V& HashMap<K, V>::get(const K& key) {
    size_t index = hash(key);
    for (auto& node : table[index]) {
        if (node.key == key) {
            return node.value;
        }
    }
    throw out_of_range("Key not found");
}

template<typename K, typename V>
void HashMap<K, V>::vectorized_get(const std::vector<K>& keys, std::vector<V>& results) {
    int i = 0;
    for(auto &key : keys){
        size_t index = hash(key);
        bool found = false;
        for (auto& node : table[index]) {
            if (node.key == key) {
                results.at(i) = node.value;
                found = true;
                break;
            }
        }
        if(!found){
            throw out_of_range("Key not found");
        }
        i++;
    }
}

template<typename K, typename V>
void HashMap<K, V>::vectorized_get_gp(const std::vector<K>& keys, std::vector<V>& results) {
    // Vector to keep track of states for each key
    std::vector<int> states(keys.size(), -1);
    // states:
    // -1: Get first node
    //  0: Prefetch next list node
    //  1: Finished, element found

    std::vector<typename std::list<Node<K, V>>::iterator> nodes;

    for (auto& key : keys) {
        __builtin_prefetch(&table[hash(key)], 0, 3);
    }

    int finished = 0;
    while (finished < keys.size()) {
        for (int i = 0; i < keys.size(); i++) {
            int& state = states[i];
            if (state == -1) {
                size_t index = hash(keys[i]);
                nodes.emplace_back(table[index].begin());
                __builtin_prefetch(&(*nodes.back()), 0, 3);
                state = 0;
            } else if (state == 0) {
                auto& node = nodes[i];
                if (node->key == keys[i]) {
                    results[i] = node->value;
                    state = 1;
                    ++finished;
                    continue;
                }
                ++node;
                if (node != table[hash(keys[i])].end()) {
                    __builtin_prefetch(reinterpret_cast<char *>(&(*node)), 0, 3);
                } else {
                    throw out_of_range("Key not found.");
                }
            } else {
                // Finished processing this key
                continue;
            }
        }
    }
}



template<typename K, typename V>
void HashMap<K, V>::vectorized_get_amac(const std::vector<K>& keys, std::vector<V>& results, int group_size) {
    CircularBuffer<AMAC_state> buff(group_size);

    // Initialize variables
    int num_finished = 0;
    int i = 0;
    int j = 0;
    while (num_finished < keys.size()) {
        AMAC_state& state = buff.next_state();

        if (state.stage == 0) {
            if(i >= keys.size()){
                continue;
            }
            state.i = i;
            state.key = keys[i++];
            state.node = table[hash(state.key)].begin();
            state.end = table[hash(state.key)].end();
            state.stage = 1;
            __builtin_prefetch(&(*state.node), 0, 3);
        } else if (state.stage == 1) {
            if (state.key == state.node->key) {
                state.stage = 0;
                results[state.i] = state.node->value;
                num_finished++;
            } else {
                ++state.node;
                if (state.node == state.end) {
                    throw out_of_range("Key not found.");
                }
                __builtin_prefetch(&(*state.node), 0, 3);
            }
        }
    }
}


template<typename K, typename V>
coroutine HashMap<K, V>::get_co(const K& key, std::vector<V>& results, const int i){
    size_t index = hash(key);

    // prefetch bucket (list head)
    __builtin_prefetch(&table[index], 0, 3);
    co_await std::suspend_always{};


    auto node = table[index].begin();
    auto end = table[index].end();
    while (node != end) {
        __builtin_prefetch(&(*node), 0, 3);
        co_await std::suspend_always{};
        if (node->key == key) {
            results.at(i) = node->value;
            co_return;
        }
        ++node;
    }
    throw out_of_range("Key not found");
}

template <typename K, typename V>
coroutine HashMap<K, V>::get_co_exp(const K &key, std::vector<V> &results, const int i)
{
    size_t index = hash(key);

    // prefetch bucket(list head)
    if (!is_in_tlb_and_prefetch(&table[index]))
    {
        co_await std::suspend_always{};
    }

    auto node = table[index].begin();
    auto end = table[index].end();
    while (node != end)
    {
        if (!is_in_tlb_and_prefetch(&(*node)))
        {
            co_await std::suspend_always{};
        }
        if (node->key == key)
        {
            results.at(i) = node->value;
            co_return;
        }
        ++node;
    }
    throw out_of_range("Key not found");
}

template <typename K, typename V>
coroutine HashMap<K, V>::profile_get_co_exp(const K &key, std::vector<V> &results, const int i)
{
    size_t prefetch_count = 0;
    bool assume_cached = true;
    size_t index = hash(key);

    // if (!is_in_tlb_prefetch_profile(&table[index], prefetch_count, profiler, assume_cached))
    //{
    co_await std::suspend_always{};
    //}

    auto node = table[index].begin();
    auto end = table[index].end();
    while (node != end)
    {
        // if (!is_in_tlb_prefetch_profile(&(*node), prefetch_count, profiler, assume_cached))
        //{
        co_await std::suspend_always{};
        //}
        if (node->key == key)
        {
            results.at(i) = node->value;
            co_return;
        }
        ++node;
    }
    throw out_of_range("Key not found");
}

template<typename K, typename V>
void HashMap<K, V>::vectorized_get_coroutine(const std::vector<K>& keys, std::vector<V>& results, int group_size) {
    CircularBuffer<coroutine_handle<promise>> buff(min(group_size,  static_cast<int>(keys.size())));

    int num_finished = 0;
    int i = 0;

    while (num_finished < keys.size())
    {
        coroutine_handle<promise>& handle = buff.next_state();
        if (!handle)
        {
            if (i < min(group_size, static_cast<int>(keys.size())))
            {
                handle = get_co(keys[i], results, i);
                i++;
            }
            continue;
        }

        if (handle.done()) {
            num_finished++;
            handle.destroy();
            if (i < keys.size()) {
                handle = get_co(keys[i], results, i);
                ++i;
            } else {
                handle = nullptr;
                continue;
            }
        }

        handle.resume();
    }
}

template <typename K, typename V>
void HashMap<K, V>::profile_vectorized_get_coroutine_exp(const std::vector<K> &keys, std::vector<V> &results, int group_size)
{
    CircularBuffer<coroutine_handle<promise>> buff(min(group_size, static_cast<int>(keys.size())));

    int num_finished = 0;
    int i = 0;

    while (num_finished < keys.size())
    {
        coroutine_handle<promise> &handle = buff.next_state();
        if (!handle)
        {
            if (i < min(group_size, static_cast<int>(keys.size())))
            {
                handle = profile_get_co_exp(keys[i], results, i);
                i++;
            }
            continue;
        }

        if (handle.done())
        {
            num_finished++;
            handle.destroy();
            if (i < keys.size())
            {
                handle = profile_get_co_exp(keys[i], results, i);
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

template <typename K, typename V>
void HashMap<K, V>::vectorized_get_coroutine_exp(const std::vector<K> &keys, std::vector<V> &results, int group_size)
{
    CircularBuffer<coroutine_handle<promise>> buff(min(group_size, static_cast<int>(keys.size())));

    int num_finished = 0;
    int i = 0;

    while (num_finished < keys.size())
    {
        coroutine_handle<promise> &handle = buff.next_state();
        if (!handle)
        {
            if (i < min(group_size, static_cast<int>(keys.size())))
            {
                handle = get_co_exp(keys[i], results, i);
                i++;
            }
            continue;
        }

        if (handle.done())
        {
            num_finished++;
            handle.destroy();
            if (i < keys.size())
            {
                handle = get_co_exp(keys[i], results, i);
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

template<typename K, typename V>
void HashMap<K, V>::remove(const K& key) {
    size_t index = hash(key);
    for (auto it = table[index].begin(); it != table[index].end(); ++it) {
        if ((*it).key == key) {
            table[index].erase(it);
            size--;
            return;
        }
    }
    throw out_of_range("Key not found");
}

template<typename K, typename V>
bool HashMap<K, V>::contains(const K& key) {

    size_t index = hash(key);
    for (auto& node : table[index]) {
        if (node.key == key) {
            return true;
        }
    }
    return false;
}

template<typename K, typename V>
size_t HashMap<K, V>::getSize() const {
    return size;
}

template<typename K, typename V>
bool HashMap<K, V>::isEmpty() const {
    return size == 0;
}


template class HashMap<unsigned int, unsigned int>;
