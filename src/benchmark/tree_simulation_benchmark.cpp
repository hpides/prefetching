#include "prefetching.hpp"

#include <random>
#include <functional>
#include <chrono>
#include <assert.h>
#include <numa.h>
#include <numaif.h>
#include <thread>
#include <iostream>
#include <fstream>

#include <nlohmann/json.hpp>

#include "numa/numa_memory_resource.hpp"
#include "numa/interleaving_numa_memory_resource.hpp"
#include "utils/utils.cpp"
#include "coroutine.hpp"

enum CoSlotState : uint8_t
{
    Empty,
    Finished,
    Resumable,
    Remote,
    Done
};

/*
    TODO: run on SMT (aka all logical cores on a physical core)
    We run the schedulers in groups, each group containing exactly one core per NUMA node.
    Each scheduler "owns" a thread_frame where it stores coroutines that currently run on this thread.
    Furthermore, scheduler 1 owns the first <coroutine_n> slots, scheduler 2 the next n slots, making sure
    new work is pushed when coroutines are finished.
*/
struct thread_frame // TODO: align this struct to Cache line
{
    std::atomic<CoSlotState> *running_coroutines = nullptr;
    std::atomic<task *> *coroutines = nullptr;
    thread_frame(size_t number_of_coroutines)
    {
        running_coroutines = new std::atomic<CoSlotState>[number_of_coroutines];
        for (size_t i = 0; i < number_of_coroutines; i++)
        {
            running_coroutines[i] = Empty;
        }
        coroutines = new std::atomic<task *>[number_of_coroutines];
    }
    ~thread_frame()
    {
        delete[] running_coroutines;
        delete[] coroutines;
    }
};

struct scheduler_thread_info
{
    NodeID curr_group_node_id = 9999;
    std::vector<std::atomic<thread_frame *>> *tfs = nullptr;
    size_t curr_coroutine_id = 0;
};
thread_local scheduler_thread_info SCHEDULER_THREAD_INFO;

template <typename handle>
auto jump_between_threads(std::vector<thread_frame *> &thread_frames, NodeID from, NodeID to, int coroutine_id)
{
    struct awaitable
    {
        std::vector<thread_frame *> &thread_frames;
        NodeID from;
        NodeID to;
        int coroutine_id;
        bool await_ready() { return false; }
        void await_suspend(handle h)
        {
            thread_frame *from_node = thread_frames[from];
            thread_frame *to_node = thread_frames[to];
            from_node->running_coroutines[coroutine_id] = 0;
            from_node->coroutines[coroutine_id] = nullptr;
            to_node->coroutines[coroutine_id] = h;

            // make sure this is the last thing that happens
            // thread can instantly pick up. Perhaps mem barriers
            // should be used.
            to_node->running_coroutines[coroutine_id] = 1;
        }
        void await_resume() {}
    };
    return awaitable{thread_frames, from, to, coroutine_id};
}

struct TreeSimulationConfig
{
    size_t tree_node_size;
    NodeID numa_nodes;
    size_t memory_per_node;
    size_t num_threads;
    size_t coroutines;
    size_t num_lookups;
    size_t num_node_traversal_per_lookup;
};

unsigned
find_in_node(uint32_t *node, uint32_t k, uint32_t values_per_node)
{
    unsigned lower = 0;
    unsigned upper = values_per_node;
    do
    {
        unsigned mid = ((upper - lower) / 2) + lower;
        if (k < node[mid])
        {
            upper = mid;
        }
        else if (k > node[mid])
        {
            lower = mid + 1;
        }
        else
        {
            return mid;
        }
    } while (lower < upper);
    throw std::runtime_error("could not find value in node " + std::to_string(k));
    return values_per_node;
}

task co_find_in_node(uint32_t *node, uint32_t k, uint32_t values_per_node, uint32_t &found_value)
{
    unsigned lower = 0;
    unsigned upper = values_per_node;
    do
    {
        unsigned mid = ((upper - lower) / 2) + lower;
        __builtin_prefetch(reinterpret_cast<void *>(node + mid), 0, 3);
        co_await std::suspend_always{};
        if (k < node[mid])
        {
            upper = mid;
        }
        else if (k > node[mid])
        {
            lower = mid + 1;
        }
        else
        {
            found_value = mid;
            co_return;
        }
    } while (lower < upper);
    throw std::runtime_error("could not find value in node " + std::to_string(k));
    co_return;
}

task co_tree_traversal(TreeSimulationConfig &config, char *data, uint32_t k, uint32_t values_per_node,
                       std::uniform_int_distribution<> node_distribution, auto gen)
{
    int sum = 0;
    for (int j = 0; j < config.num_node_traversal_per_lookup; j++)
    {
        auto next_node = node_distribution(gen);
        uint32_t found;
        co_find_in_node(reinterpret_cast<uint32_t *>(data + (next_node * config.tree_node_size)), k, values_per_node, found);
        sum += found;
    }
    if (sum != config.num_node_traversal_per_lookup * k)
    {
        "lookups failed " + std::to_string(sum) + " vs. " + std::to_string(config.num_node_traversal_per_lookup * k);
    }
    co_return;
}

auto jump_to_other_node(size_t curr_node_id, size_t target_node_id, size_t starting_node)
{
    struct awaitable
    {
        size_t curr_node_id;
        size_t target_node_id;
        size_t starting_node;
        bool await_ready() { return false; }
        void await_suspend(task h)
        {
            h.next_node = target_node_id;
        }
        void await_resume() {}
    };
    return awaitable{curr_node_id, target_node_id, starting_node};
}

template <typename handle>
auto handle_exit(size_t curr_node_id, size_t starting_node)
{
    struct awaitable
    {
        size_t curr_node_id;
        size_t starting_node;
        bool await_ready() { return false; }
        void await_suspend(handle h)
        {
            auto const curr_node_id = SCHEDULER_THREAD_INFO.curr_group_node_id;
            if (curr_node_id != starting_node)
            {
                auto &tfs = *(SCHEDULER_THREAD_INFO.tfs);
                auto const &local_tf = tfs[curr_node_id].load();
                local_tf->running_coroutines[SCHEDULER_THREAD_INFO.curr_coroutine_id] = Empty;
                auto const &starting_tf = tfs[starting_node].load();
                starting_tf->running_coroutines[SCHEDULER_THREAD_INFO.curr_coroutine_id] = Resumable;
            }
        }
        void await_resume() {}
    };
    return awaitable{curr_node_id, starting_node};
}

task co_tree_traversal_jumping(TreeSimulationConfig &config, char *data, uint32_t k, uint32_t values_per_node,
                               std::uniform_int_distribution<> node_distribution, auto gen)
{
    auto starting_node = SCHEDULER_THREAD_INFO.curr_group_node_id;

    int sum = 0;
    for (int j = 0; j < config.num_node_traversal_per_lookup; j++)
    {
        auto next_node = node_distribution(gen);

        // handle node jumping
        auto target_node = Prefetching::get().numa_manager.interleaving_memory_resource.value().node_id(reinterpret_cast<void *>(next_node));
        auto const curr_node_id = SCHEDULER_THREAD_INFO.curr_group_node_id;
        if (target_node != curr_node_id)
        {
            co_await jump_to_other_node(curr_node_id, target_node, starting_node);
        }
        // handling complete
        uint32_t found;
        co_find_in_node(reinterpret_cast<uint32_t *>(data + (next_node * config.tree_node_size)), k, values_per_node, found);
        sum += found;
    }
    if (sum != config.num_node_traversal_per_lookup * k)
    {
        "lookups failed " + std::to_string(sum) + " vs. " + std::to_string(config.num_node_traversal_per_lookup * k);
    }
    co_return;
}

void scheduler_thread_function(NodeID cpu, std::vector<std::atomic<thread_frame *>> &thread_frames, char *data, size_t group_thread_id, size_t values_per_node,
                               size_t num_tree_nodes, std::atomic<size_t> &finished, TreeSimulationConfig config)
{
    pin_to_cpu(cpu);

    auto num_total_coroutine_slots = config.coroutines * config.numa_nodes;
    auto tf = new thread_frame{num_total_coroutine_slots};
    thread_frames[group_thread_id] = tf;
    SCHEDULER_THREAD_INFO.curr_group_node_id = group_thread_id;
    SCHEDULER_THREAD_INFO.tfs = &thread_frames;

    size_t num_finished = 0;
    size_t num_scheduled = 0;
    auto repetitions = config.num_lookups / config.num_threads;
    auto tf_start_slot = config.coroutines * group_thread_id;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> uniform_dis_node_value(0, values_per_node - 1);
    std::uniform_int_distribution<> uniform_dis_next_node(0, num_tree_nodes - 1);

    while (true)
    {
        for (size_t i = 0; i < num_total_coroutine_slots; ++i)
        {
            SCHEDULER_THREAD_INFO.curr_coroutine_id = i;
            if (tf_start_slot <= i && i < (tf_start_slot + config.coroutines))
            {
                switch (tf->running_coroutines[i])
                {
                case Empty:
                case Finished: // empty, insert new
                {
                    if (num_scheduled == repetitions)
                    {
                        tf->running_coroutines[i] = Done;
                        continue;
                    }
                    auto k = uniform_dis_node_value(gen);
                    tf->coroutines[i] = new task(co_tree_traversal(config, data, k, values_per_node,
                                                                   uniform_dis_next_node, gen));
                    tf->coroutines[i].load()->next_node = group_thread_id;
                    num_scheduled++;
                    tf->running_coroutines[i] = Resumable;
                }
                break;
                case Resumable:
                    if (!tf->coroutines[i].load()->coro.done())
                    {
                        tf->coroutines[i].load()->coro.resume();
                        if (tf->coroutines[i].load()->next_node != group_thread_id)
                        {
                            tf->running_coroutines[i] = Remote;
                            thread_frames[tf->coroutines[i].load()->next_node].load()->coroutines[i] = tf->coroutines[i].load();
                            sfence();
                            thread_frames[tf->coroutines[i].load()->next_node].load()->running_coroutines[i] = Resumable;
                        }
                    }
                    else
                    {
                        delete tf->coroutines[i];
                        tf->coroutines[i] = nullptr;
                        tf->running_coroutines[i] = Finished;
                        num_finished++;
                        if (num_finished == repetitions)
                        {
                            finished++;
                        }
                        if (num_finished > repetitions)
                        {
                            throw std::runtime_error("Num_finished > repetitions.");
                        }
                    }
                    break;
                case Done:   // finished scheduling, ignore
                case Remote: // Coroutine on other node, ignore
                    break;
                default:
                    throw std::runtime_error("unknown state in running coroutines");
                    break;
                }
            }
            else
            {
                switch (tf->running_coroutines[i])
                {
                case Resumable:
                    if (!tf->coroutines[i].load()->coro.done())
                    {
                        tf->coroutines[i].load()->coro.resume();
                        if (tf->coroutines[i].load()->next_node != group_thread_id)
                        {
                            tf->running_coroutines[i] = Empty;
                            thread_frames[tf->coroutines[i].load()->next_node].load()->coroutines[i] = tf->coroutines[i].load();
                            sfence();
                            thread_frames[tf->coroutines[i].load()->next_node].load()->running_coroutines[i] = Resumable;
                        }
                    }
                    else
                    {
                        size_t original_node = i / config.coroutines;
                        tf->running_coroutines[i] = Empty;
                        sfence();
                        thread_frames[original_node].load()->running_coroutines[i] = Resumable;
                    }
                    break;
                case Empty:
                    break;
                default:
                    throw std::runtime_error("Remote slot encountered invalid state " + std::to_string(tf->running_coroutines[i]));
                    break;
                }
            }
        }
        if (num_finished == repetitions)
        {
            if (finished.load() == config.numa_nodes)
            {
                return;
            }
        }
    }
}

void scheduler_thread_function_jumping(NodeID cpu, std::vector<std::atomic<thread_frame *>> &thread_frames, char *data, size_t group_thread_id, size_t values_per_node,
                                       size_t num_tree_nodes, std::atomic<size_t> &finished, TreeSimulationConfig config)
{
    pin_to_cpu(cpu);

    auto num_total_coroutine_slots = config.coroutines * config.numa_nodes;
    auto tf = new thread_frame{num_total_coroutine_slots};
    thread_frames[group_thread_id] = tf;
    SCHEDULER_THREAD_INFO.curr_group_node_id = group_thread_id;
    SCHEDULER_THREAD_INFO.tfs = &thread_frames;

    size_t num_finished = 0;
    size_t num_scheduled = 0;
    auto repetitions = config.num_lookups / config.num_threads;
    auto tf_start_slot = config.coroutines * group_thread_id;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> uniform_dis_node_value(0, values_per_node - 1);
    std::uniform_int_distribution<> uniform_dis_next_node(0, num_tree_nodes - 1);

    while (true)
    {
        for (size_t i = 0; i < num_total_coroutine_slots; ++i)
        {
            SCHEDULER_THREAD_INFO.curr_coroutine_id = i;
            if (tf_start_slot <= i && i < (tf_start_slot + config.coroutines))
            {
                switch (tf->running_coroutines[i])
                {
                case Empty:
                case Finished: // empty, insert new
                {
                    if (num_scheduled == repetitions)
                    {
                        tf->running_coroutines[i] = Done;
                        continue;
                    }
                    auto k = uniform_dis_node_value(gen);
                    tf->coroutines[i] = new task(co_tree_traversal_jumping(config, data, k, values_per_node,
                                                                           uniform_dis_next_node, gen));
                    num_scheduled++;
                    tf->running_coroutines[i] = Resumable;
                }
                break;
                case Resumable:
                {
                    bool resumable_found = false;
                    size_t last_resumable = 0;
                    for (size_t g = 0; g < thread_frames.size(); g++)
                    {
                        while (thread_frames[g] == nullptr)
                        {
                        }; // wait for all threads to be up and running
                        if (thread_frames[g].load()->running_coroutines[i] == Resumable)
                        {
                            if (resumable_found)
                            {
                                if (thread_frames[last_resumable].load()->running_coroutines[i] == Resumable)
                                {
                                    throw std::runtime_error("Coroutine Resumable on multiple threads");
                                }
                            }
                            resumable_found = true;
                            last_resumable = g;
                        }
                    }
                };

                    if (!tf->coroutines[i].load()->coro.done())
                    {
                        tf->coroutines[i].load()->coro.resume();
                    }
                    else
                    {
                        delete tf->coroutines[i];
                        tf->coroutines[i] = nullptr;
                        tf->running_coroutines[i] = Finished;
                        num_finished++;
                        if (num_finished == repetitions)
                        {
                            finished++;
                        }
                        if (num_finished > repetitions)
                        {
                            throw std::runtime_error("Num_finished > repetitions.");
                        }
                    }
                    break;
                case Done:   // finished scheduling, ignore
                case Remote: // Coroutine on other node, ignore
                    break;
                default:
                    throw std::runtime_error("unknown state in running coroutines");
                    break;
                }
            }
            else
            {
                switch (tf->running_coroutines[i])
                {
                case Resumable:
                    if (!tf->coroutines[i].load()->coro.done())
                    {
                        tf->coroutines[i].load()->coro.resume();
                    }
                    else
                    {
                        size_t original_node = i / config.coroutines;
                        tf->running_coroutines[i] = Empty;
                        thread_frames[original_node].load()->running_coroutines[i] = Resumable;
                    }
                    break;
                case Empty:
                    break;
                default:
                    throw std::runtime_error("Remote slot encountered invalid state " + std::to_string(tf->running_coroutines[i]));
                    break;
                }
            }
        }
        if (num_finished == repetitions)
        {
            if (finished.load() == config.numa_nodes)
            {
                return;
            }
        }
    }
}

void initialize_scheduler_groups(char *data, std::vector<NodeID> cpus, size_t values_per_node, size_t num_tree_nodes, TreeSimulationConfig config)
{
    std::vector<std::jthread> threads;
    std::vector<std::atomic<thread_frame *>> thread_frames(cpus.size());
    for (int i = 0; i < cpus.size(); ++i)
    {
        thread_frames[i] = nullptr;
    }
    std::atomic<size_t> finished = 0;
    for (int i = 1; i < cpus.size(); ++i)
    {
        threads.emplace_back(scheduler_thread_function, cpus[i], std::ref(thread_frames), data, i, values_per_node, num_tree_nodes, std::ref(finished), config);
    }
    scheduler_thread_function(cpus[0], thread_frames, data, 0, values_per_node, num_tree_nodes, std::ref(finished), config); // this thread also works as a scheduler;
    for (auto &t : threads)
    {
        t.join();
    }
}

void initialize_scheduler_groups_jumping(char *data, std::vector<NodeID> cpus, size_t values_per_node, size_t num_tree_nodes, TreeSimulationConfig config)
{
    std::vector<std::jthread> threads;
    std::vector<std::atomic<thread_frame *>> thread_frames(cpus.size());
    for (int i = 0; i < cpus.size(); ++i)
    {
        thread_frames[i] = nullptr;
    }
    std::atomic<size_t> finished = 0;
    for (int i = 1; i < cpus.size(); ++i)
    {
        threads.emplace_back(scheduler_thread_function_jumping, cpus[i], std::ref(thread_frames), data, i, values_per_node, num_tree_nodes, std::ref(finished), config);
    }
    scheduler_thread_function_jumping(cpus[0], thread_frames, data, 0, values_per_node, num_tree_nodes, std::ref(finished), config); // this thread also works as a scheduler;
    for (auto &t : threads)
    {
        t.join();
    }
}

void tree_simulation_coroutine(TreeSimulationConfig config, size_t repetitions, size_t values_per_node, size_t num_tree_nodes, char *data)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> uniform_dis_node_value(0, values_per_node - 1);
    std::uniform_int_distribution<> uniform_dis_next_node(0, num_tree_nodes - 1);
    CircularBuffer<task> buff(config.coroutines);

    size_t num_finished = 0;
    int num_scheduled = 0;

    while (num_finished < repetitions)
    {
        task &handle = buff.next_state();
        if (handle.empty || handle.coro.done())
        {
            if (num_scheduled < repetitions)
            {
                auto k = uniform_dis_node_value(gen);
                handle = co_tree_traversal(config, data, k, values_per_node,
                                           uniform_dis_next_node, gen);
                num_scheduled++;
            }
            if (num_scheduled > config.coroutines)
            {
                num_finished++;
            }
            continue;
        }

        handle.coro.resume();
    }
}

void benchmark_tree_simulation(TreeSimulationConfig &config)
{

    InterleavingNumaMemoryResource mem_res{config.numa_nodes};
    auto total_memory = config.memory_per_node * 1024 * 1024 * config.numa_nodes; // memory given in MiB
    std::pmr::vector<char> data(total_memory, &mem_res);

    auto num_tree_nodes = total_memory / config.tree_node_size;
    auto values_per_node = config.tree_node_size / sizeof(u_int32_t); // we use 4B "keys"
    for (size_t i = 0; i < num_tree_nodes; i++)
    {
        for (uint32_t j = 0; j < values_per_node; ++j)
        {
            *(reinterpret_cast<uint32_t *>(data.data() + (config.tree_node_size * i)) + j) = j;
        }
    }

    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::jthread> threads;
    for (size_t t = 0; t < config.num_threads; ++t)
    {
        threads.emplace_back([&]()
                             {
                                  std::random_device rd;
                                  std::mt19937 gen(rd());
                                  std::uniform_int_distribution<> uniform_dis_node_value(0, values_per_node-1);
                                  std::uniform_int_distribution<> uniform_dis_next_node(0, num_tree_nodes-1);
                                  for (size_t i = 0; i < config.num_lookups / config.num_threads; ++i)
                                  {
                                      int next_node;
                                      int searched_value = uniform_dis_node_value(gen);
                                      auto test_counter = 0;
                                      for (size_t j = 0; j < config.num_node_traversal_per_lookup; ++j)
                                      {
                                          next_node =  uniform_dis_next_node(gen);
                                          test_counter += find_in_node(reinterpret_cast<uint32_t *>(data.data() + (config.tree_node_size * next_node)), searched_value, values_per_node);
                                      }
                                      if (test_counter != config.num_node_traversal_per_lookup * searched_value)
                                      {
                                          throw std::runtime_error("lookups failed " + std::to_string(test_counter) + " vs. " + std::to_string(config.num_node_traversal_per_lookup * searched_value));
                                      }
                                  } });
    }
    for (auto &t : threads)
    {
        t.join();
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "multithreaded lookup took: " << std::chrono::duration<double>(end - start).count() << " seconds" << std::endl;

    start = std::chrono::high_resolution_clock::now();
    threads.clear();
    for (size_t t = 0; t < config.num_threads; ++t)
    {
        threads.emplace_back(tree_simulation_coroutine, config, config.num_lookups / config.num_threads, values_per_node,
                             num_tree_nodes, data.data());
    }
    for (auto &t : threads)
    {
        t.join();
    }
    end = std::chrono::high_resolution_clock::now();
    std::cout << "multithreaded coroutine lookup took: " << std::chrono::duration<double>(end - start).count() << " seconds" << std::endl;

    start = std::chrono::high_resolution_clock::now();
    threads.clear();
    auto node_2_cpus = Prefetching::get().numa_manager.node_to_available_cpus;
    for (size_t t = 0; t < config.num_threads / config.numa_nodes; ++t) // We effectively schedule config.numa_nodes threads per run here.
    {
        std::vector<NodeID> cpus(config.numa_nodes, 0);
        for (int i = 0; i < config.numa_nodes; ++i)
        {
            cpus[i] = node_2_cpus[i][t];
        }
        threads.emplace_back(initialize_scheduler_groups, data.data(), cpus, values_per_node, num_tree_nodes, config);
    }
    for (auto &t : threads)
    {
        t.join();
    }
    end = std::chrono::high_resolution_clock::now();
    std::cout << "multithreaded coroutine lookup took: " << std::chrono::duration<double>(end - start).count() << " seconds" << std::endl;

    start = std::chrono::high_resolution_clock::now();
    threads.clear();
    for (size_t t = 0; t < config.num_threads / config.numa_nodes; ++t) // We effectively schedule config.numa_nodes threads per run here.
    {
        std::vector<NodeID> cpus(config.numa_nodes, 0);
        for (int i = 0; i < config.numa_nodes; ++i)
        {
            cpus[i] = node_2_cpus[i][t];
        }
        threads.emplace_back(initialize_scheduler_groups_jumping, data.data(), cpus, values_per_node, num_tree_nodes, config);
    }
    for (auto &t : threads)
    {
        t.join();
    }
    end = std::chrono::high_resolution_clock::now();
    std::cout << "multithreaded coroutine with jumping lookup took: " << std::chrono::duration<double>(end - start).count() << " seconds" << std::endl;
}

int main(int argc, char **argv)
{
    auto &benchmark_config = Prefetching::get().runtime_config;

    // clang-format off
    benchmark_config.add_options()
        ("tree_node_size", "Tree Node size in Bytes", cxxopts::value<std::vector<size_t>>()->default_value("512"))
        ("numa_nodes", "Number of numa nodes to run on", cxxopts::value<std::vector<NodeID>>()->default_value("2"))
        ("memory_per_node", "Memory allocated per numa node in MiB", cxxopts::value<std::vector<size_t>>()->default_value("2048"))
        ("num_threads", "Number of num_threads", cxxopts::value<std::vector<size_t>>()->default_value("8"))
        ("num_lookups", "Number of lookups", cxxopts::value<std::vector<size_t>>()->default_value("10000000"))
        ("num_node_traversal_per_lookup", "Number of distinct node traversals per lookup", cxxopts::value<std::vector<size_t>>()->default_value("10"))
        ("coroutines", "Number of coroutines per thread", cxxopts::value<std::vector<size_t>>()->default_value("20"));
    // clang-format on
    benchmark_config.parse(argc, argv);

    int benchmark_run = 0;
    for (auto &runtime_config : benchmark_config.get_runtime_configs())
    {
        auto tree_node_size = convert<size_t>(runtime_config["tree_node_size"]);
        auto numa_nodes = convert<NodeID>(runtime_config["numa_nodes"]);
        auto memory_per_node = convert<size_t>(runtime_config["memory_per_node"]);
        auto num_threads = convert<size_t>(runtime_config["num_threads"]);
        auto coroutines = convert<size_t>(runtime_config["coroutines"]);
        auto num_lookups = convert<size_t>(runtime_config["num_lookups"]);
        auto num_node_traversal_per_lookup = convert<size_t>(runtime_config["num_node_traversal_per_lookup"]);
        TreeSimulationConfig config = {tree_node_size, numa_nodes, memory_per_node, num_threads, coroutines, num_lookups, num_node_traversal_per_lookup};
        nlohmann::json results;

        benchmark_tree_simulation(config);
        auto results_file = std::ofstream{"tree_simulation_" + std::to_string(benchmark_run++) + ".json"};
        results_file << results.dump(-1) << std::flush;
    }

    return 0;
}