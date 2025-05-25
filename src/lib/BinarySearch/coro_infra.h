#pragma once
/*
Copyright (c) 2018 Gor Nishanov All rights reserved.

This code is licensed under the MIT License (MIT).

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <stdint.h>
#include <stddef.h>
#include <coroutine>
#include <exception>
#include <stdio.h>
#include <stdlib.h>
///// --- INFRASTRUCTURE CODE BEGIN ---- ////

struct scheduler_queue
{
  static constexpr const int N = 256;
  using coro_handle = std::coroutine_handle<>;

  uint32_t head = 0;
  uint32_t tail = 0;
  coro_handle arr[N];

  void push_back(coro_handle h)
  {
    arr[head] = h;
    head = (head + 1) % N;
  }

  coro_handle pop_front()
  {
    auto result = arr[tail];
    tail = (tail + 1) % N;
    return result;
  }
  auto try_pop_front() { return head != tail ? pop_front() : coro_handle{}; }

  void run()
  {
    while (auto h = try_pop_front())
      h.resume();
  }
};

inline scheduler_queue thread_local scheduler;

// prefetch Awaitable
template <const bool reliability, typename T>
struct prefetch_Awaitable
{
  T &value;

  prefetch_Awaitable(T &value) : value(value) {}

  bool await_ready() { return false; }
  T &await_resume() { return value; }
  template <typename Handle>
  auto await_suspend(Handle h)
  {
    if constexpr (reliability)
    {
      auto reliability_mask = uintptr_t(1) << 60;
      auto masked_address = reinterpret_cast<void *>(reinterpret_cast<std::uintptr_t>(std::addressof(value)) | reliability_mask);
      __builtin_prefetch(masked_address, 0);
    }
    else
    {
      __builtin_prefetch(std::addressof(value), 0); // changed to __builtin_prefetch and addressof(), replaced _MM_HINT_NTA with 0
    }
    auto &q = scheduler;
    q.push_back(h);
    return q.pop_front();
  }
};

template <const bool reliability, typename T>
auto prefetch(T &value)
{
  return prefetch_Awaitable<reliability, T>{value};
}

// Simple thread caching allocator.
struct tcalloc
{
  struct header
  {
    header *next;
    size_t size;
  };
  header *root = nullptr;
  size_t last_size_allocated = 0;
  size_t total = 0;
  size_t alloc_count = 0;

  ~tcalloc()
  {
    auto current = root;
    while (current)
    {
      auto next = current->next;
      ::free(current);
      current = next;
    }
  }

  void *alloc(size_t sz)
  {
    if (root && root->size >= sz) // modified vs original
    {
      void *mem = root;
      root = root->next;
      return mem;
    }
    ++alloc_count;
    total += sz;
    last_size_allocated = sz;

    return malloc(sz);
  }

  void stats()
  {
    printf("allocs %zu total %zu sz %zu\n", alloc_count, total, last_size_allocated);
  }

  void free(void *p, size_t sz)
  {
    auto new_entry = static_cast<header *>(p);
    new_entry->size = sz;
    new_entry->next = root;
    root = new_entry;
  }
};

inline tcalloc thread_local allocator;

struct throttler;

struct root_task
{
  struct promise_type;
  using HDL = std::coroutine_handle<promise_type>;

  struct promise_type
  {
    throttler *owner = nullptr;

    void *operator new(size_t sz) { return allocator.alloc(sz); }
    void operator delete(void *p, size_t sz) { allocator.free(p, sz); }

    root_task get_return_object() { return root_task{*this}; }
    std::suspend_always initial_suspend() { return {}; }
    void return_void();
    void unhandled_exception() noexcept { std::terminate(); }
    std::suspend_never final_suspend() noexcept { return {}; }
  };

  // TODO: this can be done via a wrapper coroutine
  auto set_owner(throttler *owner)
  {
    auto result = h;
    h.promise().owner = owner;
    h = nullptr;
    return result;
  }

  ~root_task()
  {
    if (h)
      h.destroy();
  }

  root_task(root_task &&rhs) : h(rhs.h) { rhs.h = nullptr; }
  root_task(root_task const &) = delete;

private:
  root_task(promise_type &p) : h(HDL::from_promise(p)) {}

  HDL h;
};

struct throttler
{
  unsigned limit;

  explicit throttler(unsigned limit) : limit(limit) {}

  void on_task_done() { ++limit; }

  void spawn(root_task t)
  {
    if (limit == 0)
      scheduler.pop_front().resume();

    auto h = t.set_owner(this);
    scheduler.push_back(h);
    --limit;
  }

  void run()
  {
    scheduler.run();
  }

  ~throttler() { run(); }
};

void root_task::promise_type::return_void() { owner->on_task_done(); }

///// --- INFRASTRUCTURE CODE END ---- ////
