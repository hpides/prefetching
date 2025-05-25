#include <stdint.h>
#include <cstddef>
#include <vector>
#include "coroutine.hpp"

template <typename V>
class RandomAccess
{
private:
    V *data;
    size_t num_elements;

public:
    RandomAccess(size_t num_elements);
    V &get(size_t pos);
    coroutine get_co(size_t pos, std::vector<V> &results, int i);
    coroutine get_co_exp(size_t pos, std::vector<V> &results, int i);
    void vectorized_get(const std::vector<size_t> &positions, std::vector<V> &results);
    void vectorized_get_gp(const std::vector<size_t> &positions, std::vector<V> &results);
    void vectorized_get_amac(const std::vector<size_t> &positions, std::vector<V> &results, size_t group_size);
    void vectorized_get_coroutine(const std::vector<size_t> &positions, std::vector<V> &results, size_t group_size);
    void vectorized_get_coroutine_exp(const std::vector<size_t> &positions, std::vector<V> &results, size_t group_size);
    size_t getSize() const;
};