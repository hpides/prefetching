/**
 * @file    no_partitioning_join.c
 * @author  Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date    Sun Feb  5 20:16:58 2012
 * @version $Id: no_partitioning_join.c 3017 2012-12-07 10:56:20Z bcagri $
 *
 * @brief  The implementation of NPO, No Partitioning Optimized join algortihm.
 *
 * (c) 2012, ETH Zurich, Systems Group
 *
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>              /* CPU_ZERO, CPU_SET */
#include <pthread.h>            /* pthread_* */
#include <string.h>             /* memset */
#include <stdio.h>              /* printf */
#include <stdlib.h>             /* memalign */
#include <sys/time.h>           /* gettimeofday */

#include "no_partitioning_join.h"
#include "npj_params.h"         /* constant parameters */
#include "npj_types.h"          /* bucket_t, hashtable_t, bucket_buffer_t */
#include "rdtsc.h"              /* startTimer, stopTimer */
#include "lock.h"               /* lock, unlock */
#include "cpu_mapping.h"        /* get_cpu_id */
#ifdef PERF_COUNTERS
#include "perf_counters.h"      /* PCM_x */
#endif

#include "barrier.h"            /* pthread_barrier_* */
#include "affinity.h"           /* pthread_attr_setaffinity_np */
#include "generator.h"          /* numa_localize() */

//#include "../../gem5/util/m5/m5op.h"

#include<assert.h>

#ifndef BARRIER_ARRIVE
/** barrier wait macro */
#define BARRIER_ARRIVE(B,RV)                            \
    RV = pthread_barrier_wait(B);                       \
    if(RV !=0 && RV != PTHREAD_BARRIER_SERIAL_THREAD){  \
        printf("Couldn't wait on barrier\n");           \
        exit(EXIT_FAILURE);                             \
    }
#endif

#ifndef NEXT_POW_2
/**
 *  compute the next number, greater than or equal to 32-bit unsigned v.
 *  taken from "bit twiddling hacks":
 *  http://graphics.stanford.edu/~seander/bithacks.html
 */
#define NEXT_POW_2(V)                           \
    do {                                        \
        V--;                                    \
        V |= V >> 1;                            \
        V |= V >> 2;                            \
        V |= V >> 4;                            \
        V |= V >> 8;                            \
        V |= V >> 16;                           \
        V++;                                    \
    } while(0)
#endif

#ifndef HASH
#define HASH(X, MASK, SKIP) (((X) & MASK) >> SKIP)
#endif

/** Debug msg logging method */
#ifdef DEBUG
#define DEBUGMSG(COND, MSG, ...)                                    \
    if(COND) { fprintf(stdout, "[DEBUG] "MSG, ## __VA_ARGS__); }
#else
#define DEBUGMSG(COND, MSG, ...)
#endif

/** An experimental feature to allocate input relations numa-local */
extern int numalocalize;  /* defined in generator.c */
extern int nthreads;      /* defined in generator.c */

/**
 * \ingroup NPO arguments to the threads
 */
typedef struct arg_t arg_t;

struct arg_t {
    int32_t             tid;
    hashtable_t *       ht;
    relation_t          relR;
    relation_t          relS;
    pthread_barrier_t * barrier;
    int64_t             num_results;
    bucket_buffer_t *overflowbuf;
#ifndef NO_TIMING
    /* stats about the thread */
    uint64_t timer1, timer2, timer3;
    struct timeval start, end;
    TimerCalls *timer_calls;
#endif
    NumaAllocator *numa_allocator;
    PerfCalls *perf_calls;
    bool reliability;
} ;

/**
 * @defgroup OverflowBuckets Buffer management for overflowing buckets.
 * Simple buffer management for overflow-buckets organized as a
 * linked-list of bucket_buffer_t.
 * @{
 */

/**
 * Initializes a new bucket_buffer_t for later use in allocating
 * buckets when overflow occurs.
 *
 * @param ppbuf [in,out] bucket buffer to be initialized
 */
void init_bucket_buffer(bucket_buffer_t **ppbuf, NumaAllocator *numa_allocator)
{
    bucket_buffer_t *overflowbuf;
    overflowbuf = numa_allocator->alloc_func(sizeof(bucket_buffer_t), 8);
    overflowbuf->count = 0;
    overflowbuf->next = NULL;

    *ppbuf = overflowbuf;
}

/**
 * Returns a new bucket_t from the given bucket_buffer_t.
 * If the bucket_buffer_t does not have enough space, then allocates
 * a new bucket_buffer_t and adds to the list.
 *
 * @param result [out] the new bucket
 * @param buf [in,out] the pointer to the bucket_buffer_t pointer
 */
extern inline void
get_new_bucket(bucket_t **result, bucket_buffer_t **buf, NumaAllocator *numa_allocator)
{
    if ((*buf)->count < OVERFLOW_BUF_SIZE)
    {
        *result = (*buf)->buf + (*buf)->count;
        (*buf)->count++;
    }
    else
    {
        /* need to allocate new buffer */
        bucket_buffer_t *new_buf = numa_allocator->alloc_func(sizeof(bucket_buffer_t), 8);
        new_buf->count = 1;
        new_buf->next = *buf;
        *buf = new_buf;
        *result = new_buf->buf;
    }
}

/** De-allocates all the bucket_buffer_t */
void free_bucket_buffer(bucket_buffer_t *buf, NumaAllocator *numa_allocator)
{
    do
    {
        bucket_buffer_t *tmp = buf->next;
        numa_allocator->free_func(buf, 8);
        buf = tmp;
    } while (buf);
}

/** @} */

/**
 * @defgroup NPO The No Partitioning Optimized Join Implementation
 * @{
 */

/**
 * Allocates a hashtable of NUM_BUCKETS and inits everything to 0.
 *
 * @param ht pointer to a hashtable_t pointer
 */
void allocate_hashtable(hashtable_t **ppht, uint32_t nbuckets, NumaAllocator *numa_alloc)
{
    hashtable_t *ht;

    ht = (hashtable_t *)numa_alloc->alloc_func(sizeof(hashtable_t), CACHE_LINE_SIZE);
    ht->num_buckets = nbuckets;
    NEXT_POW_2((ht->num_buckets));

    /* allocate hashtable buckets cache line aligned */
    ht->buckets = numa_alloc->alloc_func(ht->num_buckets * sizeof(bucket_t), CACHE_LINE_SIZE);

    /** Not an elegant way of passing whether we will numa-localize, but this
        feature is experimental anyway. */
    if (numalocalize)
    {
        tuple_t *mem = (tuple_t *)ht->buckets;
        uint32_t ntuples = (ht->num_buckets * sizeof(bucket_t)) / sizeof(tuple_t);
        numa_localize(mem, ntuples, nthreads);
    }

    memset(ht->buckets, 0, ht->num_buckets * sizeof(bucket_t));
    ht->skip_bits = 0; /* the default for modulo hash */
    ht->hash_mask = (ht->num_buckets - 1) << ht->skip_bits;
    *ppht = ht;
}

/**
 * Releases memory allocated for the hashtable.
 *
 * @param ht pointer to hashtable
 */
void destroy_hashtable(hashtable_t *ht, NumaAllocator *numa_allocator)
{
    numa_allocator->free_func(ht->buckets, CACHE_LINE_SIZE);
    numa_allocator->free_func(ht, CACHE_LINE_SIZE);
}

/**
 * Single-thread hashtable build method, ht is pre-allocated.
 *
 * @param ht hastable to be built
 * @param rel the build relation
 */
void build_hashtable_st(hashtable_t *ht, relation_t *rel, NumaAllocator *numa_allocator)
{
    uint32_t i;
    const uint32_t hashmask = ht->hash_mask;
    const uint32_t skipbits = ht->skip_bits;

    for(i=0; i < rel->num_tuples; i++){
        tuple_t * dest;
        bucket_t * curr, * nxt;
        int32_t idx = HASH(rel->tuples[i].key, hashmask, skipbits);

        /* copy the tuple to appropriate hash bucket */
        /* if full, follow nxt pointer to find correct place */
        curr = ht->buckets + idx;
        nxt  = curr->next;

        if(curr->count == BUCKET_SIZE) {
            if(!nxt || nxt->count == BUCKET_SIZE) {
                bucket_t * b;
                b = numa_allocator->alloc_func(sizeof(bucket_t), 8);
                memset(b, 0, sizeof(bucket_t));
                curr->next = b;
                b->next = nxt;
                b->count = 1;
                dest = b->tuples;
            }
            else {
                dest = nxt->tuples + nxt->count;
                nxt->count ++;
            }
        }
        else {
            dest = curr->tuples + curr->count;
            curr->count ++;
        }
        *dest = rel->tuples[i];
    }
}

#include <time.h>

clock_t start, end;
double cpu_time_used;

/**
 * Probes the hashtable for the given outer relation, returns num results.
 * This probing method is used for both single and multi-threaded version.
 *
 * @param ht hashtable to be probed
 * @param rel the probing outer relation
 *
 * @return number of matching tuples
 */
int64_t
probe_hashtable(hashtable_t *ht, relation_t *rel)
{
    uint32_t i, j;
    int64_t matches;
    const uint32_t hashmask = ht->hash_mask;
    const uint32_t skipbits = ht->skip_bits;

#if 1
    size_t prefetch_index = 8;
#endif

    matches = 0;
    start = clock();
    for (i = 0; i < rel->num_tuples; i++)
    {
        intkey_t idx = HASH(rel->tuples[i].key, hashmask, skipbits);
        bucket_t *b = ht->buckets + idx;
        tuple_t *tuples = b->tuples;
        do
        {
            for (j = 0; j < b->count; j++)
            {
                if (rel->tuples[i].key == tuples[j].key)
                {
                    matches++;
                    /* TODO: we don't materialize the results. */
                }
            }
            b = b->next; /* follow overflow pointer */
        } while (b);
    }

    end = clock();
    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("time : %f\n", cpu_time_used);
    return matches;
}

/**
 * Probes the hashtable for the given outer relation, returns num results.
 * This probing method is used for both single and multi-threaded version.
 *
 * @param ht hashtable to be probed
 * @param rel the probing outer relation
 *
 * @return number of matching tuples
 */
int64_t
probe_hashtable_swpf(hashtable_t *ht, relation_t *rel, bool reliability)
{
    uint32_t i, j;
    int64_t matches;

    const uint32_t hashmask = ht->hash_mask;
    const uint32_t skipbits = ht->skip_bits;

    matches = 0;
    start = clock();
    for (i = 0; i < rel->num_tuples; i++)
    {

#ifdef STRIDE
        __builtin_prefetch(&(rel->tuples[i + (FETCHDIST)]));
#endif
        intkey_t idx_prefetch = HASH(rel->tuples[i + (FETCHDIST >> 1)].key,
                                     hashmask, skipbits);
        if (reliability)
        {
            __builtin_prefetch((void *)((uintptr_t)(ht->buckets + idx_prefetch) | (1UL << 60)));
        }
        else
        {
            __builtin_prefetch(ht->buckets + idx_prefetch);
        }

        intkey_t idx = HASH(rel->tuples[i].key, hashmask, skipbits);
        bucket_t *b = ht->buckets + idx;
        tuple_t *tuples = b->tuples;
        do
        {
            for (j = 0; j < b->count; j++)
            {
                if (rel->tuples[i].key == tuples[j].key)
                {
                    matches++;
                    /* TODO: we don't materialize the results. */
                }
            }
            b = b->next; /* follow overflow pointer */
        } while (b);
    }

    end = clock();
    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("time : %f\n", cpu_time_used);
    return matches;
}

/** print out the execution time statistics of the join */
static void
print_timing(uint64_t total, uint64_t build, uint64_t part,
             uint64_t numtuples, int64_t result,
             struct timeval *start, struct timeval *end)
{
    double diff_usec = (((*end).tv_sec * 1000000L + (*end).tv_usec) - ((*start).tv_sec * 1000000L + (*start).tv_usec));
    double cyclestuple = total;
    cyclestuple /= numtuples;
    fprintf(stdout, "RUNTIME TOTAL, BUILD, PART (cycles): \n");
    fprintf(stderr, "%lu \t %lu \t %lu ",
            total, build, part);
    fprintf(stdout, "\n");
    fprintf(stdout, "TOTAL-TIME-USECS, TOTAL-TUPLES, CYCLES-PER-TUPLE: \n");
    fprintf(stdout, "%.4lf \t %lu \t ", diff_usec, result);
    fflush(stdout);
    fprintf(stderr, "%.4lf ", cyclestuple);
    fflush(stderr);
    fprintf(stdout, "\n");
}

/** \copydoc NPO_st */
int64_t
NPO_st(relation_t *relR, relation_t *relS, int nthreads, size_t *cpu_ids, TimerCalls *timer_calls, NumaAllocator *numa_alloc, PerfCalls *perf_calls, bool reliability)
{
    hashtable_t * ht;
    int64_t result = 0;
#ifndef NO_TIMING
    struct timeval start, end;
    uint64_t timer1, timer2, timer3;
#endif
    uint32_t nbuckets = (relR->num_tuples / (BUCKET_SIZE)); //CHANGES
    allocate_hashtable(&ht, nbuckets, numa_alloc);

#ifndef NO_TIMING
    gettimeofday(&start, NULL);
    startTimer(&timer1);
    startTimer(&timer2);
    timer3 = 0; /* no partitioning */
    timer_calls->build_start();
#endif

    build_hashtable_st(ht, relR, numa_alloc);

#ifndef NO_TIMING
    timer_calls->build_stop();
    stopTimer(&timer2); /* for build */
    timer_calls->probe_start();
#endif

    result = probe_hashtable(ht, relS);

#ifndef NO_TIMING
    timer_calls->probe_finish();
    stopTimer(&timer1); /* over all */
    gettimeofday(&end, NULL);
    /* now print the timing results: */
    print_timing(timer1, timer2, timer3, relS->num_tuples, result, &start, &end);
#endif

    destroy_hashtable(ht, numa_alloc);

    return result;
}

/**
 * Multi-thread hashtable build method, ht is pre-allocated.
 * Writes to buckets are synchronized via latches.
 *
 * @param ht hastable to be built
 * @param rel the build relation
 * @param overflowbuf pre-allocated chunk of buckets for overflow use.
 */
void build_hashtable_mt(hashtable_t *ht, relation_t *rel,
                        bucket_buffer_t **overflowbuf, NumaAllocator *numa_allocator)
{
    uint32_t i;
    const uint32_t hashmask = ht->hash_mask;
    const uint32_t skipbits = ht->skip_bits;

#if 0
    size_t prefetch_index = 16;
#endif

    for(i=0; i < rel->num_tuples; i++){
        tuple_t * dest;
        bucket_t * curr, * nxt;

#if 0
        if (prefetch_index < rel->num_tuples) {
            intkey_t idx_prefetch = HASH(rel->tuples[prefetch_index++].key,
                                         hashmask, skipbits);
			__builtin_prefetch(ht->buckets + idx_prefetch);
        }
#endif

        int32_t idx = HASH(rel->tuples[i].key, hashmask, skipbits);
        /* copy the tuple to appropriate hash bucket */
        /* if full, follow nxt pointer to find correct place */
        curr = ht->buckets+idx;
        lock(&curr->latch);
        nxt = curr->next;

        if(curr->count == BUCKET_SIZE) {
            if(!nxt || nxt->count == BUCKET_SIZE) {
                bucket_t * b;
                /* b = (bucket_t*) calloc(1, sizeof(bucket_t)); */
                /* instead of calloc() everytime, we pre-allocate */
                get_new_bucket(&b, overflowbuf, numa_allocator);
                curr->next = b;
                b->next    = nxt;
                b->count   = 1;
                dest       = b->tuples;
            }
            else {
                dest = nxt->tuples + nxt->count;
                nxt->count ++;
            }
        }
        else {
            dest = curr->tuples + curr->count;
            curr->count ++;
        }

        *dest = rel->tuples[i];
        unlock(&curr->latch);
    }

}

/**
 * Multi-thread hashtable build method, ht is pre-allocated.
 * Writes to buckets are synchronized via latches.
 *
 * @param ht hastable to be built
 * @param rel the build relation
 * @param overflowbuf pre-allocated chunk of buckets for overflow use.
 */
void build_hashtable_mt_swpf(hashtable_t *ht, relation_t *rel,
                             bucket_buffer_t **overflowbuf, NumaAllocator *numa_allocator,
                             bool reliability)
{
    uint32_t i;
    const uint32_t hashmask = ht->hash_mask;
    const uint32_t skipbits = ht->skip_bits;

    size_t prefetch_index = PREFETCH_DISTANCE << 1; // Do same Bit shift as in probe (divide by)

    for (i = 0; i < rel->num_tuples; i++)
    {
        tuple_t *dest;
        bucket_t *curr, *nxt;

        if (prefetch_index < rel->num_tuples)
        {
            intkey_t idx_prefetch = HASH(rel->tuples[prefetch_index++].key,
                                         hashmask, skipbits);
            if (reliability)
            {
                __builtin_prefetch((void *)((uintptr_t)(ht->buckets + idx_prefetch) | (1UL << 60)));
            }
            else
            {
                __builtin_prefetch(ht->buckets + idx_prefetch);
            }
        }

        int32_t idx = HASH(rel->tuples[i].key, hashmask, skipbits);
        /* copy the tuple to appropriate hash bucket */
        /* if full, follow nxt pointer to find correct place */
        curr = ht->buckets + idx;
        lock(&curr->latch);
        nxt = curr->next;

        if (curr->count == BUCKET_SIZE)
        {
            if (!nxt || nxt->count == BUCKET_SIZE)
            {
                bucket_t *b;
                /* b = (bucket_t*) calloc(1, sizeof(bucket_t)); */
                /* instead of calloc() everytime, we pre-allocate */
                get_new_bucket(&b, overflowbuf, numa_allocator);
                curr->next = b;
                b->next = nxt;
                b->count = 1;
                dest = b->tuples;
            }
            else
            {
                dest = nxt->tuples + nxt->count;
                nxt->count++;
            }
        }
        else
        {
            dest = curr->tuples + curr->count;
            curr->count++;
        }

        *dest = rel->tuples[i];
        unlock(&curr->latch);
    }
}

/**
 * Just a wrapper to call the build for each thread.
 *
 * @param param the parameters of the thread, i.e. tid, ht, reln, ...
 *
 * @return
 */
void *
npo_thread_build(void *param)
{
    int rv;
    arg_t * args = (arg_t*) param;

    /* allocate overflow buffer for each thread */
    bucket_buffer_t * overflowbuf;
    init_bucket_buffer(&overflowbuf, args->numa_allocator);
    args->overflowbuf = overflowbuf;

    /* wait at a barrier until each thread starts and start timer */
    BARRIER_ARRIVE(args->barrier, rv);

    if (args->perf_calls->profile)
    {
        args->perf_calls->perf_start(args->tid);
    }
#ifndef NO_TIMING
    /* the first thread checkpoints the start time */
    if (args->tid == 0)
    {
        args->timer_calls->build_start();
        gettimeofday(&args->start, NULL);
        startTimer(&args->timer1);

        args->timer3 = 0; /* no partitionig phase */
    }
#endif

    /* insert tuples from the assigned part of relR to the ht */
    build_hashtable_mt(args->ht, &args->relR, &overflowbuf, args->numa_allocator);

    if (args->perf_calls->profile)
    {
        args->perf_calls->perf_stop(args->tid);
    }

    /* wait at a barrier until each thread completes build phase */
    BARRIER_ARRIVE(args->barrier, rv);

    return 0;
}

/**
 * Just a wrapper to call the probe for each thread.
 *
 * @param param the parameters of the thread, i.e. tid, ht, reln, ...
 *
 * @return
 */
void *
npo_thread_probe(void *param)
{
    int rv;
    arg_t *args = (arg_t *)param;

    if (args->perf_calls->profile)
    {
        args->perf_calls->perf_start(args->tid);
    }

#ifndef NO_TIMING
    /* build phase finished, thread-0 checkpoints the time */
    if (args->tid == 0)
    {
        args->timer_calls->build_stop();
        args->timer_calls->probe_start();

        startTimer(&args->timer2);
    }
#endif

    /* probe for matching tuples from the assigned part of relS */
    args->num_results = probe_hashtable(args->ht, &args->relS);

#ifndef NO_TIMING
    /* for a reliable timing we have to wait until all finishes */
    BARRIER_ARRIVE(args->barrier, rv);

    /* probe phase finished, thread-0 checkpoints the time */
    if (args->tid == 0)
    {
        args->timer_calls->probe_finish();
        stopTimer(&args->timer1);
        stopTimer(&args->timer2);
        gettimeofday(&args->end, NULL);
    }
#endif

    if (args->perf_calls->profile)
    {
        args->perf_calls->perf_stop(args->tid);
    }

    /* clean-up the overflow buffers */
    free_bucket_buffer(args->overflowbuf, args->numa_allocator);

    return 0;
}

/**
 * Just a wrapper to call the build and probe for each thread.
 *
 * @param param the parameters of the thread, i.e. tid, ht, reln, ...
 *
 * @return
 */
void *
npo_thread_build_swpf(void *param)
{
    int rv;
    arg_t *args = (arg_t *)param;

    /* allocate overflow buffer for each thread */
    bucket_buffer_t *overflowbuf;
    init_bucket_buffer(&overflowbuf, args->numa_allocator);
    args->overflowbuf = overflowbuf;

    /* wait at a barrier until each thread starts and start timer */
    BARRIER_ARRIVE(args->barrier, rv);

    if (args->perf_calls->profile)
    {
        args->perf_calls->perf_start(args->tid);
    }

#ifndef NO_TIMING
    /* the first thread checkpoints the start time */
    if (args->tid == 0)
    {
        args->timer_calls->build_start();
        gettimeofday(&args->start, NULL);
        startTimer(&args->timer1);

        args->timer3 = 0; /* no partitionig phase */
    }
#endif

    /* insert tuples from the assigned part of relR to the ht */
    build_hashtable_mt_swpf(args->ht, &args->relR, &overflowbuf, args->numa_allocator, args->reliability);

    if (args->perf_calls->profile)
    {
        args->perf_calls->perf_stop(args->tid);
    }
    /* wait at a barrier until each thread completes build phase */
    BARRIER_ARRIVE(args->barrier, rv);

    return 0;
}
/**
 * Just a wrapper to call the build and probe for each thread.
 *
 * @param param the parameters of the thread, i.e. tid, ht, reln, ...
 *
 * @return
 */
void *
npo_thread_probe_swpf(void *param)
{
    int rv;
    arg_t *args = (arg_t *)param;

    if (args->perf_calls->profile)
    {
        args->perf_calls->perf_start(args->tid);
    }

#ifndef NO_TIMING
    /* build phase finished, thread-0 checkpoints the time */
    if (args->tid == 0)
    {
        args->timer_calls->build_stop();
        args->timer_calls->probe_start();

        startTimer(&args->timer2);
    }
#endif

    /* probe for matching tuples from the assigned part of relS */
    args->num_results = probe_hashtable_swpf(args->ht, &args->relS, args->reliability);

#ifndef NO_TIMING
    /* for a reliable timing we have to wait until all finishes */
    BARRIER_ARRIVE(args->barrier, rv);

    /* probe phase finished, thread-0 checkpoints the time */
    if (args->tid == 0)
    {
        args->timer_calls->probe_finish();
        stopTimer(&args->timer1);
        stopTimer(&args->timer2);
        gettimeofday(&args->end, NULL);
    }
#endif
    if (args->perf_calls->profile)
    {
        args->perf_calls->perf_stop(args->tid);
    }

    /* clean-up the overflow buffers */
    free_bucket_buffer(args->overflowbuf, args->numa_allocator);

    return 0;
}

/** \copydoc NPO */
int64_t
NPO(relation_t *relR, relation_t *relS, int nthreads,
    size_t *cpu_ids, TimerCalls *timer_calls, NumaAllocator *numa_alloc,
    PerfCalls *perf_calls, bool reliability)
{
    hashtable_t * ht;
    int64_t result = 0;
    int32_t numR, numS, numRthr, numSthr; /* total and per thread num */
    int i, rv;
    cpu_set_t set;
    arg_t args[nthreads];
    pthread_t tid[nthreads];
    pthread_attr_t attr;
    pthread_barrier_t barrier;

    uint32_t nbuckets = (relR->num_tuples / (BUCKET_SIZE)); //CHANGES
    allocate_hashtable(&ht, nbuckets, numa_alloc);

    numR = relR->num_tuples;
    numS = relS->num_tuples;
    numRthr = numR / nthreads;
    numSthr = numS / nthreads;

    rv = pthread_barrier_init(&barrier, NULL, nthreads);
    if(rv != 0){
        printf("Couldn't create the barrier\n");
        exit(EXIT_FAILURE);
    }
    args[0].timer_calls = timer_calls;

    pthread_attr_init(&attr);
    for (i = 0; i < nthreads; i++)
    {
        args[i].tid = i;
        args[i].ht = ht;
        args[i].barrier = &barrier;
        args[i].numa_allocator = numa_alloc;
        args[i].perf_calls = perf_calls;

        /* assing part of the relR for next thread */
        args[i].relR.num_tuples = (i == (nthreads-1)) ? numR : numRthr;
        args[i].relR.tuples = relR->tuples + numRthr * i;
        numR -= numRthr;

        /* assing part of the relS for next thread */
        args[i].relS.num_tuples = (i == (nthreads-1)) ? numS : numSthr;
        args[i].relS.tuples = relS->tuples + numSthr * i;
        numS -= numSthr;
    }

    for (i = 0; i < nthreads; i++)
    {
        int cpu_idx = cpu_ids[i];
        CPU_ZERO(&set);
        CPU_SET(cpu_idx, &set);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &set);
        rv = pthread_create(&tid[i], &attr, npo_thread_build, (void *)&args[i]);
        if (rv)
        {
            printf("ERROR; return code from pthread_create() (on npo_thread_build) is %d\n", rv);
            exit(-1);
        }
    }

    for (i = 0; i < nthreads; i++)
    {
        pthread_join(tid[i], NULL);
    }

    if (perf_calls->profile)
    {
        perf_calls->perf_log(perf_calls->results, "build", relR->num_tuples);
    }

    for (i = 0; i < nthreads; i++)
    {
        int cpu_idx = cpu_ids[i];
        CPU_ZERO(&set);
        CPU_SET(cpu_idx, &set);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &set);
        rv = pthread_create(&tid[i], &attr, npo_thread_probe, (void *)&args[i]);
        if (rv){
            printf("ERROR; return code from pthread_create() (on npo_thread_probe) is %d\n", rv);
            exit(-1);
        }
    }

    for(i = 0; i < nthreads; i++){
        pthread_join(tid[i], NULL);
        /* sum up results */
        result += args[i].num_results;
    }
    if (perf_calls->profile)
    {
        perf_calls->perf_log(perf_calls->results, "probe", relS->num_tuples);
    }

#ifndef NO_TIMING
    /* now print the timing results: */
    print_timing(args[0].timer1, args[0].timer2, args[0].timer3,
                relS->num_tuples, result,
                &args[0].start, &args[0].end);
#endif

    destroy_hashtable(ht, numa_alloc);
    pthread_attr_destroy(&attr);

    return result;
}

/** \copydoc NPO */
int64_t
NPO_swpf(relation_t *relR, relation_t *relS, int nthreads, size_t *cpu_ids, TimerCalls *timer_calls, NumaAllocator *numa_alloc, PerfCalls *perf_calls, bool reliability)
{
    hashtable_t *ht;
    int64_t result = 0;
    int32_t numR, numS, numRthr, numSthr; /* total and per thread num */
    int i, rv;
    cpu_set_t set;
    arg_t args[nthreads];
    pthread_t tid[nthreads];
    pthread_attr_t attr;
    pthread_barrier_t barrier;

    uint32_t nbuckets = (relR->num_tuples / (BUCKET_SIZE)); // CHANGES
    allocate_hashtable(&ht, nbuckets, numa_alloc);

    numR = relR->num_tuples;
    numS = relS->num_tuples;
    numRthr = numR / nthreads;
    numSthr = numS / nthreads;

    rv = pthread_barrier_init(&barrier, NULL, nthreads);
    if (rv != 0)
    {
        printf("Couldn't create the barrier\n");
        exit(EXIT_FAILURE);
    }
    args[0].timer_calls = timer_calls;

    pthread_attr_init(&attr);
    for (i = 0; i < nthreads; i++)
    {
        int cpu_idx = cpu_ids[i];

        DEBUGMSG(1, "Assigning thread-%d to CPU-%d\n", i, cpu_idx);

        CPU_ZERO(&set);
        CPU_SET(cpu_idx, &set);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &set);

        args[i].tid = i;
        args[i].ht = ht;
        args[i].barrier = &barrier;
        args[i].numa_allocator = numa_alloc;
        args[i].perf_calls = perf_calls;

        /* assing part of the relR for next thread */
        args[i].relR.num_tuples = (i == (nthreads - 1)) ? numR : numRthr;
        args[i].relR.tuples = relR->tuples + numRthr * i;
        numR -= numRthr;

        /* assing part of the relS for next thread */
        args[i].relS.num_tuples = (i == (nthreads - 1)) ? numS : numSthr;
        args[i].relS.tuples = relS->tuples + numSthr * i;
        args[i].reliability = reliability;
        numS -= numSthr;
    }

    for (i = 0; i < nthreads; i++)
    {
        int cpu_idx = cpu_ids[i];
        CPU_ZERO(&set);
        CPU_SET(cpu_idx, &set);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &set);
        rv = pthread_create(&tid[i], &attr, npo_thread_build_swpf, (void *)&args[i]);
        if (rv)
        {
            printf("ERROR; return code from pthread_create() (on npo_thread_build_swpf) is %d\n", rv);
            exit(-1);
        }
    }

    for (i = 0; i < nthreads; i++)
    {
        pthread_join(tid[i], NULL);
    }

    if (perf_calls->profile)
    {
        perf_calls->perf_log(perf_calls->results, "build", relR->num_tuples);
    }

    for (i = 0; i < nthreads; i++)
    {
        int cpu_idx = cpu_ids[i];
        CPU_ZERO(&set);
        CPU_SET(cpu_idx, &set);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &set);
        rv = pthread_create(&tid[i], &attr, npo_thread_probe_swpf, (void *)&args[i]);
        if (rv)
        {
            printf("ERROR; return code from pthread_create() (on npo_thread_probe_swpf) is %d\n", rv);
            exit(-1);
        }
    }

    for (i = 0; i < nthreads; i++)
    {
        pthread_join(tid[i], NULL);
        /* sum up results */
        result += args[i].num_results;
    }

    if (perf_calls->profile)
    {
        perf_calls->perf_log(perf_calls->results, "probe", relS->num_tuples);
    }

#ifndef NO_TIMING
    /* now print the timing results: */
    print_timing(args[0].timer1, args[0].timer2, args[0].timer3,
                 relS->num_tuples, result,
                 &args[0].start, &args[0].end);
#endif

    destroy_hashtable(ht, numa_alloc);
    pthread_attr_destroy(&attr);

    return result;
}

/** @}*/

/** \copydoc NPO_st */
int64_t
NPO_st_swpf(relation_t *relR, relation_t *relS, int nthreads, size_t *cpu_ids, TimerCalls *timer_calls, NumaAllocator *numa_alloc, PerfCalls *perf_calls, bool reliability)
{
    hashtable_t *ht;
    int64_t result = 0;
#ifndef NO_TIMING
    struct timeval start, end;
    uint64_t timer1, timer2, timer3;
#endif
    uint32_t nbuckets = (relR->num_tuples / (BUCKET_SIZE)); // CHANGES
    allocate_hashtable(&ht, nbuckets, numa_alloc);

#ifndef NO_TIMING
    gettimeofday(&start, NULL);
    startTimer(&timer1);
    startTimer(&timer2);
    timer3 = 0; /* no partitioning */
    timer_calls->build_start();
#endif
    build_hashtable_st(ht, relR, numa_alloc);
#ifndef NO_TIMING
    timer_calls->build_stop();
    stopTimer(&timer2); /* for build */
    timer_calls->probe_start();
#endif
    result = probe_hashtable(ht, relS);
#ifndef NO_TIMING
    timer_calls->probe_finish();
    stopTimer(&timer1); /* over all */
    gettimeofday(&end, NULL);
    /* now print the timing results: */
    print_timing(timer1, timer2, timer3, relS->num_tuples, result, &start, &end);
#endif

    destroy_hashtable(ht, numa_alloc);

    return result;
}