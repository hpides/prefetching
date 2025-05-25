#include <random>
#include <iostream>
#include <fstream>
#include <xmmintrin.h>
#include <vector>
#include <algorithm>
#include <cstring>

const size_t DATA_SIZE = 1u << 29; // 512 MiB
const size_t LOADS = 100'000'000;
int main()
{
    char *data = reinterpret_cast<char *>(malloc(1u << 29));
    memset(data, DATA_SIZE, 0);
    std::vector<std::uint64_t> accesses(LOADS);
    std::vector<std::uint64_t> load_accesses(LOADS);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, DATA_SIZE - 1);

    std::generate(accesses.begin(), accesses.end(), [&]()
                  { return dis(gen); });
    std::generate(load_accesses.begin(), load_accesses.end(), [&]()
                  { return dis(gen); });
    volatile char sum = 0;
    for (unsigned i = 0; i < LOADS; ++i)
    {
        sum += *(data + load_accesses[i] + sum);
        __builtin_prefetch(data + accesses[i] + sum, 0, 0);
    }
    if (sum != 0)
    {
        std::cout << "Error" << std::endl;
    }
}

// g++ test_mab_allocs.cpp -O3 -o a.out
// perf stat -e ls_mab_alloc.loads,ls_pref_instr_disp.prefetch_nta  ./a.out
// Performance counter stats for './a.out':
//
//         1.921.770      ls_mab_alloc.loads
//       100.055.720      ls_pref_instr_disp.prefetch_nta
//
//       1,820940662 seconds time elapsed
//
//       1,124275000 seconds user
//       0,696170000 seconds sys
// vs -> remove "sum += *(data + load_accesses[i] + sum);" ,aka flood mab
// g++ test_mab_allocs.cpp -O3 -o a.out
// perf stat -e ls_mab_alloc.loads,ls_pref_instr_disp.prefetch_nta  ./a.out
// Performance counter stats for './a.out':
//
//       207.760.577      ls_mab_alloc.loads
//       101.936.954      ls_pref_instr_disp.prefetch_nta
//
//       4,247183759 seconds time elapsed
//
//       3,462914000 seconds user
//       0,783754000 seconds sys