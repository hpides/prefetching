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

#include <vector>
#include <span>
#include <functional>
#include <stdio.h>
#include <memory_resource> // added

#include "coro_infra.h"

template <const bool reliability, typename Iterator, typename Found, typename NotFound>
root_task CoroBinarySearch(Iterator first, Iterator last, int val,
                           Found on_found, NotFound on_not_found)

{
  auto len = last - first;
  while (len > 0)
  {
    auto half = len / 2;
    auto middle = first + half;
    auto x = co_await prefetch<reliability>(*middle);
    if (x < val)
    {
      first = middle;
      ++first;
      len = len - half - 1;
    }
    else
      len = half;
    if (x == val)
      co_return on_found(middle);
  }
  on_not_found();
}

template <const bool reliability>
long CoroMultiLookup(
    std::pmr::vector<int> const &v, std::span<int> const &lookups, int streams) // added pmr
{

  size_t found_count = 0;
  size_t not_found_count = 0;

  throttler t(streams);

  for (auto key : lookups)
    t.spawn(CoroBinarySearch<reliability>(v.begin(), v.end(), key, [&](auto it)
                                          { ++found_count; }, [&]()
                                          { ++not_found_count; }));

  t.run();

  if (found_count + not_found_count != lookups.size())
    printf("BUG: found %zu, not-found: %zu total %zu\n", found_count,
           not_found_count, found_count + not_found_count);

  return found_count;
}
