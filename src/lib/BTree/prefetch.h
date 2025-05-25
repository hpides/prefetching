#pragma once

#include <cstdint>

class SWPrefetcher
{
public:
    static uintptr_t reliability_mask;
    enum class Target : std::uint8_t
    {
        ALL = 3U,
        L2 = 2U,
        L3 = 1U,
        NTA = 0U
    };

    template<std::uint32_t F, std::uint32_t C, Target T = Target::ALL>
    inline static void prefetch(std::int64_t *data)
    {
        constexpr auto items_per_cacheline = 64U / sizeof(std::int64_t);
        for (auto i = F * items_per_cacheline; i < (C + F) * items_per_cacheline; i += items_per_cacheline)
        {
            auto masked_address = reinterpret_cast<void *>(reinterpret_cast<std::uintptr_t>(&data[i]) | reliability_mask);
            __builtin_prefetch(masked_address, 0, static_cast<std::uint8_t>(T));
        }
    }

    template<std::uint32_t F, std::uint32_t C, Target T = Target::ALL>
    inline static void prefetch(void *data)
    {
        prefetch<F, C, T>(reinterpret_cast<std::int64_t*>(data));
    }
};
