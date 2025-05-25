#include <coroutine>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <thread>
#include <atomic>

const int NUMBER_COROUTINES = 20;

struct task
{
    struct promise_type
    {
        task get_return_object() { return task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
    std::coroutine_handle<promise_type> coro;
    task(std::coroutine_handle<promise_type> h) : coro(h) {}
    task()
    {
        coro = {};
    }
};

struct thread_frame
{
    std::atomic<bool> running_coroutines[NUMBER_COROUTINES] = {false};
    std::atomic<task> coroutines[NUMBER_COROUTINES];
};

auto switch_to_new_thread(std::jthread &out)
{
    struct awaitable
    {
        std::jthread *p_out;
        bool await_ready() { return false; }
        void await_suspend(std::coroutine_handle<> h)
        {
            std::jthread &out = *p_out;
            if (out.joinable())
                throw std::runtime_error("Output jthread parameter not empty");
            out = std::jthread([h]
                               { h.resume(); });
            std::cout << "New thread ID: " << out.get_id() << '\n';
        }
        void await_resume() {}
    };
    return awaitable{&out};
}

template <typename handle>
auto jump_between_threads(thread_frame *node_1, thread_frame *node_2, int coroutine_id)
{
    struct awaitable
    {
        thread_frame *node_1;
        thread_frame *node_2;
        int coroutine_id;
        bool await_ready() { return false; }
        void await_suspend(handle h)
        {
            if (node_1->running_coroutines[coroutine_id])
            {
                node_2->coroutines[coroutine_id] = h;
                node_1->running_coroutines[coroutine_id] = false;
                node_2->running_coroutines[coroutine_id] = true;
            }
            else
            {
                node_1->coroutines[coroutine_id] = h;
                node_2->running_coroutines[coroutine_id] = false;
                node_1->running_coroutines[coroutine_id] = true;
            }
        }
        void await_resume() {}
    };
    return awaitable{node_1, node_2, coroutine_id};
}

void thread_function(thread_frame *frame)
{
    while (true)
    {
        for (int i = 0; i < NUMBER_COROUTINES; ++i)
        {
            if (frame->running_coroutines[i])
            {
                if (!frame->coroutines[i].load().coro.done())
                {
                    frame->coroutines[i].load().coro.resume();
                }
            }
        }
        std::this_thread::yield();
    }
}

task try_jumping_between_threads(thread_frame *node_1, thread_frame *node_2, int coroutine_id)
{
    for (int i = 0; i < 10'000'000; ++i)
    {
        co_await jump_between_threads<task>(node_1, node_2, coroutine_id);
    }
    std::cout << "jumped 10'000'000 times" << std::endl;
    node_1->running_coroutines[coroutine_id] = false;
    node_2->running_coroutines[coroutine_id] = false;
}

void thread_main_function(std::vector<int> cpu_ids)
{
    std::vector<std::jthread> threads;
    std::vector<thread_frame *> thread_frames;
    for (auto cpu_id : cpu_ids)
    {
        thread_frame *frame = new thread_frame;
        threads.emplace_back(thread_function, frame);
        thread_frames.push_back(frame);
    }

    for (int i = 0; i < NUMBER_COROUTINES; ++i)
    {
        auto handle = try_jumping_between_threads(thread_frames[0], thread_frames[1], i);
        thread_frames[0]->coroutines[i] = handle;
        thread_frames[0]->running_coroutines[i] = true;
    }
}

int main()
{
    std::vector<int> cpu_ids = {0, 1};
    std::jthread main(thread_main_function, cpu_ids);
    main.join();
}