/**
 * @file    types.h
 * @author  Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date    Tue May 22 16:43:30 2012
 * @version $Id: types.h 3017 2012-12-07 10:56:20Z bcagri $
 *
 * @brief  Provides general type definitions used by all join algorithms.
 *
 *
 */
#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @defgroup Types Common Types
 * Common type definitions used by all join implementations.
 * @{
 */

#ifdef KEY_8B /* 64-bit key/value, 16B tuples */
typedef int64_t intkey_t;
typedef int64_t value_t;
#else /* 32-bit key/value, 8B tuples */
typedef int32_t intkey_t;
typedef int32_t value_t;
#endif

typedef struct tuple_t    tuple_t;
typedef struct relation_t relation_t;
typedef struct NumaAllocator NumaAllocator;
typedef struct TimerCalls TimerCalls;
typedef struct PerfCalls PerfCalls;

/** Type definition for a tuple, depending on KEY_8B a tuple can be 16B or 8B */
struct tuple_t {
    intkey_t key;
    value_t  payload;
};

/**
 * Type definition for a relation.
 * It consists of an array of tuples and a size of the relation.
 */
struct relation_t {
  tuple_t * tuples;
  uint32_t  num_tuples;
};

typedef void *(*AllocFunc)(size_t, size_t);
typedef void *(*FreeFunc)(void *, size_t);
typedef void (*TimerFunc)();
typedef void (*PerfFunc)(int);
typedef void (*PerfSaveFunc)(void *, char *, size_t);

struct NumaAllocator
{
  AllocFunc alloc_func;
  FreeFunc free_func;
};

struct TimerCalls
{
  TimerFunc build_start;
  TimerFunc build_stop;
  TimerFunc probe_start;
  TimerFunc probe_finish;
};

struct PerfCalls
{
  PerfFunc perf_start;
  PerfFunc perf_stop;
  PerfSaveFunc perf_log;
  void *results;
  bool profile;
};
/** @} */

#endif /* TYPES_H */
