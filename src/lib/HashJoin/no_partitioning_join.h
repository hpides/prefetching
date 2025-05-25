/**
 * @file    no_partitioning_join.h
 * @author  Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date    Sun Feb  5 20:12:56 2012
 * @version $Id: no_partitioning_join.h 3017 2012-12-07 10:56:20Z bcagri $
 *
 * @brief  The interface of No partitioning optimized (NPO) join algorithm.
 *
 * (c) 2012, ETH Zurich, Systems Group
 *
 */

#ifndef NO_PARTITIONING_JOIN_H
#define NO_PARTITIONING_JOIN_H

#include "types.h" /* relation_t */
#include "npj_types.h" /* bucket_t, hashtable_t, bucket_buffer_t */

/**
 * NPO: No Partitioning Join Optimized.
 *
 * The "No Partitioning Join Optimized" implementation denoted as NPO
 * which was originally proposed by Blanas et al. in SIGMOD 2011.
 *
 * The following is a multi-threaded implementation. Just returns the
 * number of result tuples.
 *
 * @param relR input relation R - inner relation
 * @param relS input relation S - outer relation
 * @param timer_calls callbacks to handle timers for probe and build times
 *
 * @return number of result tuples
 */
int64_t
NPO(relation_t *relR, relation_t *relS, int nthreads, size_t *cpu_ids, TimerCalls *timer_calls, NumaAllocator *numa_alloc, PerfCalls *perf_calls, bool reliability);

/**
 * NPO: No Partitioning Join Optimized.
 *
 * The "No Partitioning Join Optimized" implementation denoted as NPO
 * which was originally proposed by Blanas et al. in SIGMOD 2011.
 * Contains software prefetching optimizations.
 *
 * The following is a multi-threaded implementation. Just returns the
 * number of result tuples.
 *
 * @param relR input relation R - inner relation
 * @param relS input relation S - outer relation
 * @param timer_calls callbacks to handle timers for probe and build times
 *
 * @return number of result tuples
 */
int64_t
NPO_swpf(relation_t *relR, relation_t *relS, int nthreads, size_t *cpu_ids, TimerCalls *timer_calls, NumaAllocator *numa_alloc, PerfCalls *perf_calls, bool reliability);

/**
 * The No Partitioning Join Optimized (NPO) as a single-threaded
 * implementation. Just returns the number of result tuples.
 *
 * @param relR input relation R - inner relation
 * @param relS input relation S - outer relation
 * @param timer_calls callbacks to handle timers for probe and build times
 *
 * @return number of result tuples
 */
int64_t
NPO_st(relation_t *relR, relation_t *relS, int nthreads, size_t *cpu_ids, TimerCalls *timer_calls, NumaAllocator *numa_alloc, PerfCalls *perf_calls, bool reliability);

/**
 * The No Partitioning Join Optimized (NPO) as a single-threaded
 * implementation. Just returns the number of result tuples.
 * Contains software prefetching optimizations.
 *
 * @param relR input relation R - inner relation
 * @param relS input relation S - outer relation
 * @param timer_calls callbacks to handle timers for probe and build times
 *
 * @return number of result tuples
 */
int64_t
NPO_st_swpf(relation_t *relR, relation_t *relS, int nthreads, size_t *cpu_ids, TimerCalls *timer_calls, NumaAllocator *numa_alloc, PerfCalls *perf_calls, bool reliability);

#endif /* NO_PARTITIONING_JOIN_H */
