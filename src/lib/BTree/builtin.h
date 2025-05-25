#pragma once

#if defined(__x86_64__)
#include <immintrin.h>
#endif

class builtin
{
public:
    static inline void pause()
    {
#if defined(__x86_64__)
        _mm_pause();
#elif defined(__aarch64__)
        asm volatile("yield");
#endif
    }
};
