#include "utils.hpp"

void wait_cycles(uint64_t x)
{
    for (int i = 0; i < x; ++i)
    {
#if defined(X86_64)
        _mm_pause();
#elif defined(AARCH64)
        __asm__ volatile("yield");
#endif
    };
}

inline unsigned long long read_cycles()
{
#if defined(X86_64)
    return __rdtsc();
#elif defined(AARCH64)
    uint64_t cycles;
    asm volatile("mrs %0, cntvct_el0" : "=r"(cycles));
    return cycles;
#endif
}

inline void lfence()
{
#if defined(X86_64)
    _mm_lfence();
#elif defined(AARCH64)
    std::atomic_thread_fence(std::memory_order::consume);
#endif
}

inline void sfence()
{
#if defined(X86_64)
    _mm_sfence();
#elif defined(AARCH64)
    std::atomic_thread_fence(std::memory_order::release);
#endif
}

const uint64_t l1_prefetch_latency = 44;

static uint64_t sampling_counter = 0;

inline bool is_in_tlb_and_prefetch(const void *ptr)
{
    uint64_t start, end;

    start = read_cycles();
    lfence();
    asm volatile("" ::: "memory");

    __builtin_prefetch(ptr, 0, 3); // Prefetch to L1 cache

    asm volatile("" ::: "memory");
    lfence();
    end = read_cycles();

    // sampling_counter++;
    // if ((sampling_counter & 0xFFFF) == 0xFFFF)
    //{
    //     std::cout << end - start << "," << std::endl;
    // }

    return (end - start) <= l1_prefetch_latency;
}

template <typename T>
class CircularBuffer
{
private:
    std::vector<T> buffer;
    size_t capacity;
    size_t next_index;

public:
    CircularBuffer(size_t capacity) : capacity(capacity), next_index(0)
    {
        buffer.resize(capacity);
    }

    T &next_state()
    {
        T &state = buffer[next_index];
        next_index = (next_index + 1) % capacity;
        return state;
    }
};

void pin_to_cpu(NodeID cpu)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    const auto return_code = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (return_code != 0)
    {
        throw std::runtime_error("pinning thread to cpu failed (return code: " + std::to_string(return_code) + ").");
    }
};

void pin_to_cpus(std::vector<NodeID> &cpus)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (auto cpu : cpus)
    {
        CPU_SET(cpu, &cpuset);
    }
    const auto return_code = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (return_code != 0)
    {
        throw std::runtime_error("pinning thread to cpus failed (return code: " + std::to_string(return_code) + ").");
    }
};

std::vector<std::pair<long long, long long>> get_memory_stats_per_numa_node()
{
    if (numa_available() == -1)
    {
        throw std::runtime_error("NUMA is not available on this system.");
    }

    int num_nodes = numa_max_node() + 1;
    std::vector<std::pair<long long, long long>> memory_stats(num_nodes, {0, 0});

    for (int node = 0; node < num_nodes; ++node)
    {
        long long free_ram = 0;
        long long total_ram = numa_node_size64(node, &free_ram);
        if (total_ram == -1)
        {
            std::cerr << "Error retrieving information for node " << node << std::endl;
            memory_stats[node] = {total_ram, -1};
        }
        else
        {
            memory_stats[node] = {total_ram, free_ram};
        }
    }

    return memory_stats;
}

void initialize_pointer_chase(uint64_t *data, size_t size)
{
    std::vector<uint64_t> random_numbers(size);

    std::iota(random_numbers.begin(), random_numbers.end(), 0);
    auto rng = std::mt19937{42};
    std::shuffle(random_numbers.begin(), random_numbers.end(), rng);

    auto zero_it = std::find(random_numbers.begin(), random_numbers.end(), 0);
    unsigned curr = 0;
    for (auto behind_zero = zero_it + 1; behind_zero < random_numbers.end(); behind_zero++)
    {
        *(data + curr) = *behind_zero;
        curr = *behind_zero;
    }
    for (auto before_zero = random_numbers.begin(); before_zero <= zero_it; before_zero++)
    {
        *(data + curr) = *before_zero;
        curr = *before_zero;
    }

    // verify pointer chase:
    // auto jumper = data;
    // auto data_first = reinterpret_cast<uint64_t>(data);
    // uint64_t counter = 1;
    // while (*jumper != 0)
    //{
    //    jumper = reinterpret_cast<uint64_t *>(data + *jumper);
    //    counter++;
    //}
    // if (counter != size)
    //{
    //    throw std::runtime_error("pointer chase init failed. Expected jumps : " + std::to_string(size) + " actual jumps: " + std::to_string(counter));
    //}
}

// --- "Work" taken from https://dl.acm.org/doi/10.1145/3662010.3663451 ---

inline std::uint32_t murmur_32_scramble(std::uint32_t k)
{
    /// Murmur3 32bit https://en.wikipedia.org/wiki/MurmurHash
    k *= 0xcc9e2d51;
    k = (k << 15) | (k >> 17);
    k *= 0x1b873593;
    return k;
}

inline std::uint32_t murmur_32(std::uint32_t val)
{
    /// Murmur3 32bit https://en.wikipedia.org/wiki/MurmurHash
    auto h = 19553029U;

    /// Scramble
    h ^= murmur_32_scramble(val);
    h = (h << 13U) | (h >> 19U);
    h = h * 5U + 0xe6546b64;

    /// Finalize
    h ^= sizeof(std::uint32_t);
    h ^= h >> 16U;
    h *= 0x85ebca6b;
    h ^= h >> 13U;
    h *= 0xc2b2ae35;
    h ^= h >> 16U;

    return h;
}

// --- End "Work" ---

size_t align_to_power_of_floor(size_t p, size_t align)
{
    return p & ~(align - 1);
}

template <typename T>
size_t get_data_size_in_bytes(std::pmr::vector<T> &vec)
{
    return vec.size() * sizeof(T);
}

auto get_steady_clock_min_duration(size_t repetitions)
{
    // warm up
    for (size_t i = 0; i < 50'000'000; ++i)
    {
        asm volatile("" ::: "memory");
        auto start = std::chrono::steady_clock::now();
        asm volatile("" ::: "memory");
        auto end = std::chrono::steady_clock::now();
        asm volatile("" ::: "memory");
    }

    std::vector<std::chrono::duration<double>> durations(repetitions);
    for (size_t i = 0; i < repetitions; ++i)
    {
        asm volatile("" ::: "memory");
        auto start = std::chrono::steady_clock::now();
        asm volatile("" ::: "memory");
        auto end = std::chrono::steady_clock::now();
        asm volatile("" ::: "memory");
        durations[i] = end - start;
    }
    return findMedian(durations, durations.size());
}

void ensure(auto exp, auto message)
{
    if (!exp)
    {
        throw std::runtime_error(message);
    }
}

std::string convert_to_utf8(const std::string &str)
{
    try
    {
        // Convert to wide string first, then back to UTF-8
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
        std::wstring wide_str = conv.from_bytes(str); // Throws on invalid UTF-8
        return conv.to_bytes(wide_str);               // Convert back to UTF-8
    }
    catch (const std::range_error &)
    {
        // Handle invalid UTF-8 by returning a placeholder or empty string
        return std::string("�"); // Placeholder character for invalid UTF-8
    }
}

long long round_up_to_multiple(long long num, long long x)
{
    return static_cast<long long>(std::ceil(static_cast<double>(num) / x)) * x;
}