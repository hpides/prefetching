#include <list>
#include <iostream>
#include <memory_resource>

#include "numa/static_numa_memory_resource.hpp"

class Test
{
public:
    int x = 0;
    int y = 1;
    Test(){};
};
template <typename K, typename V>
class TemplateTest
{
public:
    K key;
    V value;
};

int main()
{
    StaticNumaMemoryResource mem_res{0};

    // --- Test 1 -> Vector containing int ---
    std::cout << "--- Test 1 ---" << std::endl;

    std::pmr::vector<int> test(&mem_res);

    for (int i = 0; i < 100; ++i)
    {
        test.push_back(i);
    }

    // --- Test 2 -> Vector containing List containing int ---
    std::cout << "--- Test 2 ---" << std::endl;

    std::pmr::vector<std::pmr::list<int>> test2(&mem_res);

    std::cout << "adding 10 lists" << std::endl;
    for (int i = 0; i < 10; ++i)
    {
        test2.emplace_back(std::pmr::list<int>(&mem_res));
    }
    std::cout << "adding 10 elements to first list" << std::endl;
    for (int i = 0; i < 10; ++i)
    {
        test2[0].emplace_back(i);
    }

    // --- Test 3 -> Vector containing List containing custom class containing primitives ---
    std::cout << "--- Test 3 ---" << std::endl;

    std::pmr::vector<std::pmr::list<Test>> test3(&mem_res);

    std::cout << "adding 10 lists" << std::endl;
    for (int i = 0; i < 10; ++i)
    {
        test3.emplace_back(std::pmr::list<Test>(&mem_res));
    }
    std::cout << "adding 10 elements to first list" << std::endl;
    for (int i = 0; i < 10; ++i)
    {
        test3[0].emplace_back();
    }

    // --- Test 4 -> Vector containing List containing custom templated class ---
    std::cout << "--- Test 4 ---" << std::endl;

    std::pmr::list<TemplateTest<long, long>> test4(&mem_res);

    std::cout << "adding 10 elements to list" << std::endl;
    for (int i = 0; i < 10; ++i)
    {
        test4.emplace_back(0l, 0l);
    }
    // --- Test 5 -> Vector containing List containing custom templated class ---
    std::cout << "--- Test 5 ---" << std::endl;

    std::pmr::vector<std::pmr::vector<TemplateTest<long, long>>> test5(&mem_res);

    std::cout << "adding 10 lists" << std::endl;
    for (int i = 0; i < 10; ++i)
    {
        test5.emplace_back();
    }
    std::cout << "adding 10 elements to first list" << std::endl;
    for (int i = 0; i < 10; ++i)
    {
        test5[0].emplace_back(0l, 0l);
    }
    return 0;
}