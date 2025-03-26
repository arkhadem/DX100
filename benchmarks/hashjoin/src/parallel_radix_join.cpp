/**
 * @file    parallel_radix_join.c
 * @author  Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date    Sun Feb 20:19:51 2012
 * @version $Id: parallel_radix_join.c 3017 2012-12-07 10:56:20Z bcagri $
 * 
 * @brief  Provides implementations for several variants of Radix Hash Join.
 * 
 * (c) 2012, ETH Zurich, Systems Group
 * 
 */

#include <cstdint>
#include <cstring>
#include <omp.h>
#include <stdlib.h> /* malloc, posix_memalign */
#include <stdio.h>  /* printf */
#include <stdbool.h>
#include <cassert>

#include "parallel_radix_join.h"
#include "prj_params.h" /* constant parameters */
#include "task_queue.h" /* task_queue_* */

// #define DEBUG

#if !defined(FUNC) && !defined(GEM5) && !defined(GEM5_MAGIC)
#define FUNC
#endif

#if defined(FUNC)
#include <MAA_functional.hpp>
#elif defined(GEM5)
#include <MAA_gem5.hpp>
#include <gem5/m5ops.h>
#elif defined(GEM5_MAGIC)
#include "MAA_gem5_magic.hpp"
#endif
#include <MAA_utility.hpp>

/** checks malloc() result */
#ifndef MALLOC_CHECK
#define MALLOC_CHECK(M)                                                \
    if (!M) {                                                          \
        printf("[ERROR] MALLOC_CHECK: %s : %d\n", __FILE__, __LINE__); \
        perror(": malloc() failed!\n");                                \
        exit(EXIT_FAILURE);                                            \
    }
#endif

/* #define RADIX_HASH(V)  ((V>>7)^(V>>13)^(V>>21)^V) */
#define HASH_BIT_MODULO(K, MASK, NBITS) (((K) & MASK) >> NBITS)

#ifndef NEXT_POW_2
/** 
 *  compute the next number, greater than or equal to 32-bit unsigned v.
 *  taken from "bit twiddling hacks":
 *  http://graphics.stanford.edu/~seander/bithacks.html
 */
#define NEXT_POW_2(V) \
    do {              \
        V--;          \
        V |= V >> 1;  \
        V |= V >> 2;  \
        V |= V >> 4;  \
        V |= V >> 8;  \
        V |= V >> 16; \
        V++;          \
    } while (0)
#endif

#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

/** Debug msg logging method */
#ifdef DEBUG
#define DEBUGMSG(COND, MSG, ...)                        \
    if (COND) {                                         \
        fprintf(stdout, "[DEBUG] " MSG, ##__VA_ARGS__); \
        fflush(stdout);                                 \
    }
#else
#define DEBUGMSG(COND, MSG, ...)
#endif

/** An experimental feature to allocate input relations numa-local */
extern int numalocalize; /* defined in generator.c */

typedef struct arg_t arg_t;
typedef struct part_t part_t;
typedef int64_t (*JoinFunction)(const relation_t *const,
                                const relation_t *const,
                                relation_t *const, arg_t *);

/** holds the arguments passed to each thread */
struct arg_t {
    int32_t **histR;
    tuple_t *relR;
    tuple_t *tmpR;
    int32_t **histS;
    tuple_t *relS;
    tuple_t *tmpS;
    int32_t **outR;
    int32_t **outS;
    int32_t **dst;

    int32_t numR;
    int32_t numS;
    int32_t totalR;
    int32_t totalS;

    task_queue_t *join_queue;
    task_queue_t *part_queue;
#ifdef SKEW_HANDLING
    task_queue_t *skew_queue;
    task_t **skewtask;
#endif
    JoinFunction join_function;
    int64_t result;
    int nthreads;

    /* stats about the thread */
    int32_t parts_processed;
#ifdef MAA
    int reg0, reg1, reg2, reg3, reg4, reg5, regConst2, regConst1;
    int tile0, tile1, tile2, tile3, tile4, tile5, tile6, tile7;
#endif
} __attribute__((aligned(CACHE_LINE_SIZE)));

/** holds arguments passed for partitioning */
struct part_t {
    tuple_t *rel;
    tuple_t *tmp;
    int32_t **hist;
    int32_t *output;
    int32_t *dst;
    arg_t *thrargs;
    uint32_t num_tuples;
    uint32_t total_tuples;
    int32_t R;
    uint32_t D;
    int relidx; /* 0: R, 1: S */
    uint32_t padding;
} __attribute__((aligned(CACHE_LINE_SIZE)));

static void *
alloc_aligned(size_t size) {
    void *ret;
    int rv;
    rv = posix_memalign((void **)&ret, CACHE_LINE_SIZE, size);

    if (rv) {
        perror("alloc_aligned() failed: out of memory");
        return 0;
    }

    return ret;
}

/** \endinternal */

/** 
 * @defgroup Radix Radix Join Implementation Variants
 * @{
 */

/** 
 *  This algorithm builds the hashtable using the bucket chaining idea and used
 *  in PRO implementation. Join between given two relations is evaluated using
 *  the "bucket chaining" algorithm proposed by Manegold et al. It is used after
 *  the partitioning phase, which is common for all algorithms. Moreover, R and
 *  S typically fit into L2 or at least R and |R|*sizeof(int) fits into L2 cache.
 * 
 * @param R input relation R
 * @param S input relation S
 * 
 * @return number of result tuples
 */

static bool checked = false;

int64_t
bucket_chaining_join(const relation_t *const R,
                     const relation_t *const S,
                     relation_t *const tmpR, arg_t *thrargs) {
    int *next, *bucket;
    const uint32_t numR = R->num_tuples;
    uint32_t N = numR;
    int64_t matches = 0;

    NEXT_POW_2(N);
    /* N <<= 1; */
    const uint32_t MASK = (N - 1) << (NUM_RADIX_BITS);

    next = (int *)malloc(sizeof(int) * numR);
    /* posix_memalign((void**)&next, CACHE_LINE_SIZE, numR * sizeof(int)); */
    bucket = (int *)malloc(N * sizeof(int));
    MALLOC_CHECK(next && bucket);
    memset(bucket, 0, N * sizeof(int));

    const tuple_t *const Rtuples = R->tuples;
    for (uint32_t i = 0; i < numR;) {
        uint32_t idx = HASH_BIT_MODULO(R->tuples[i].key, MASK, NUM_RADIX_BITS);
        next[i] = bucket[idx];
        bucket[idx] = ++i; /* we start pos's from 1 instead of 0 */

        /* Enable the following tO avoid the code elimination
           when running probe only for the time break-down experiment */
        /* matches += idx; */
    }

    const tuple_t *const Stuples = S->tuples;
    const uint32_t numS = S->num_tuples;

    /* Disable the following loop for no-probe for the break-down experiments */
    /* PROBE- LOOP */
    for (uint32_t i = 0; i < numS; i++) {

        uint32_t idx = HASH_BIT_MODULO(Stuples[i].key, MASK, NUM_RADIX_BITS);

        for (int hit = bucket[idx]; hit > 0; hit = next[hit - 1]) {

            if (Stuples[i].key == Rtuples[hit - 1].key) {
                /* TODO: copy to the result buffer, we skip it */
                matches++;
            }
        }
    }
    /* PROBE-LOOP END  */

    /* clean up temp */
    free(bucket);
    free(next);

    return matches;
}

/** computes and returns the histogram size for join */
inline uint32_t
get_hist_size(uint32_t relSize) __attribute__((always_inline));

inline uint32_t
get_hist_size(uint32_t relSize) {
    NEXT_POW_2(relSize);
    relSize >>= 2;
    if (relSize < 4)
        relSize = 4;
    return relSize;
}

/**
 * Histogram-based hash table build method together with relation re-ordering as
 * described by Kim et al. It joins partitions Ri, Si of relations R & S. 
 * This is version is not optimized with SIMD and prefetching. The parallel
 * radix join implementation using this function is PRH.
 */
int64_t
histogram_join(const relation_t *const R,
               const relation_t *const S,
               relation_t *const tmpR, arg_t *thrargs) {
    int32_t *hist;
    const tuple_t *const Rtuples = R->tuples;
    const uint32_t numR = R->num_tuples;
    uint32_t Nhist = get_hist_size(numR);
    const uint32_t MASK = (Nhist - 1) << NUM_RADIX_BITS;

    hist = (int32_t *)calloc(Nhist + 2, sizeof(int32_t));

    for (uint32_t i = 0; i < numR; i++) {

        uint32_t idx = HASH_BIT_MODULO(Rtuples[i].key, MASK, NUM_RADIX_BITS);

        hist[idx + 2]++;
    }

    // // PRINT hist to test
    // for (int i = 0; i < Nhist+2; i++) {
    //     printf("hist[%d] = %d\n", i, hist[i]);
    // }

    /* prefix sum on histogram */
    for (uint32_t i = 2, sum = 0; i <= Nhist + 1; i++) {
        sum += hist[i];
        hist[i] = sum;
    }

    tuple_t *const tmpRtuples = tmpR->tuples;
    /* reorder tuples according to the prefix sum */
    for (uint32_t i = 0; i < numR; i++) {

        uint32_t idx = HASH_BIT_MODULO(Rtuples[i].key, MASK, NUM_RADIX_BITS) + 1;

        tmpRtuples[hist[idx]] = Rtuples[i];

        hist[idx]++;
    }

    int match = 0;
    const uint32_t numS = S->num_tuples;
    const tuple_t *const Stuples = S->tuples;
    for (uint32_t i = 0; i < numS; i++) {

        // Existing code
        uint32_t idx = HASH_BIT_MODULO(Stuples[i].key, MASK, NUM_RADIX_BITS);
        int j = hist[idx], end = hist[idx + 1];
        /* Scalar comparisons */
        for (; j < end; j++) {

            if (Stuples[i].key == tmpRtuples[j].key) {

                ++match;
                /* TODO: we do not output results */
            }
        }
    }
    /* clean up */
    free(hist);

    return match;
}

/** software prefetching function */
inline void
prefetch(void *addr) __attribute__((always_inline));

inline void
prefetch(void *addr) {
    /* #ifdef __x86_64__ */
    __asm__ __volatile__("prefetcht0 %0" ::"m"(*(uint32_t *)addr));
    /* _mm_prefetch(addr, _MM_HINT_T0); */
    /* #endif */
}

/** 
 * Radix clustering algorithm (originally described by Manegold et al) 
 * The algorithm mimics the 2-pass radix clustering algorithm from
 * Kim et al. The difference is that it does not compute 
 * prefix-sum, instead the sum (offset in the code) is computed iteratively.
 *
 * @warning This method puts padding between clusters, see
 * radix_cluster_nopadding for the one without padding.
 *
 * @param outRel [out] result of the partitioning
 * @param inRel [in] input relation
 * @param hist [out] number of tuples in each partition
 * @param R cluster bits
 * @param D radix bits per pass
 * @returns tuples per partition.
 */
void radix_cluster(relation_t *outRel,
                   relation_t *inRel,
                   int32_t *hist,
                   int32_t *dst,
                   int R,
                   int D,
                   arg_t *args) {
    uint32_t i;
    uint32_t M = ((1 << D) - 1) << R;
    uint32_t offset;
    uint32_t fanOut = 1 << D;

    /* count tuples per cluster */
    for (i = 0; i < inRel->num_tuples; i++) {
        int32_t idx = HASH_BIT_MODULO(inRel->tuples[i].key, M, R);
        hist[idx]++;
    }
    offset = 0;
    /* determine the start and end of each cluster depending on the counts. */
    for (i = 0; i < fanOut; i++) {
        /* dst[i]      = outRel->tuples + offset; */
        /* determine the beginning of each partitioning by adding some
           padding to avoid L1 conflict misses during scatter. */
        dst[i] = offset + i * SMALL_PADDING_TUPLES;
        offset += hist[i];
    }
    /* copy tuples to their corresponding clusters at appropriate offsets */
    for (i = 0; i < inRel->num_tuples; i++) {
        int32_t idx = HASH_BIT_MODULO(inRel->tuples[i].key, M, R);
        outRel->tuples[dst[idx]] = inRel->tuples[i];
        ++dst[idx];
    }
}

#ifdef MAA
/** 
 * Radix clustering algorithm (originally described by Manegold et al) 
 * The algorithm mimics the 2-pass radix clustering algorithm from
 * Kim et al. The difference is that it does not compute 
 * prefix-sum, instead the sum (offset in the code) is computed iteratively.
 *
 * @warning This method puts padding between clusters, see
 * radix_cluster_nopadding for the one without padding.
 *
 * @param outRel [out] result of the partitioning
 * @param inRel [in] input relation
 * @param hist [out] number of tuples in each partition
 * @param R cluster bits
 * @param D radix bits per pass
 * @returns tuples per partition.
 */
void radix_cluster_maa(relation_t *outRel,
                       relation_t *inRel,
                       int32_t *hist,
                       int32_t *dst,
                       int R,
                       int D,
                       arg_t *args) {
    uint32_t i;
    uint32_t M = ((1 << D) - 1) << R;
    uint32_t offset;
    uint32_t fanOut = 1 << D;
    int my_tid = omp_get_thread_num();

    DEBUGMSG(1, "radix_cluster_maa started: tid=%d\n", my_tid);

    int reg0, reg1, reg2, reg3, reg4, reg5, regConst2, regConst1;
    int tile0, tile1, tile2, tile3, tile4, tile5, tile6, tile7;
    reg0 = args->reg0;
    reg1 = args->reg1;
    reg2 = args->reg2;
    reg3 = args->reg3;
    reg4 = args->reg4;
    reg5 = args->reg5;
    regConst2 = args->regConst2;
    regConst1 = args->regConst1;
    tile0 = args->tile0;
    tile1 = args->tile1;
    tile2 = args->tile2;
    tile3 = args->tile3;
    tile4 = args->tile4;
    tile5 = args->tile5;
    tile6 = args->tile6;
    tile7 = args->tile7;
    maa_const<int>(inRel->num_tuples * 2, reg1);
    maa_const<int>(inRel->num_tuples, reg4);
    maa_const<int>(M, reg2);
    maa_const<int>(R, reg3);
    /* count tuples per cluster */
    // for( i=0; i < inRel->num_tuples; i++ ){
    //     uint32_t idx = HASH_BIT_MODULO(inRel->tuples[i].key, M, R);
    //     hist[idx]++;
    // }
    int *inRel_tuples_int = (int *)inRel->tuples;
    double *inRel_tuples_double = (double *)inRel->tuples;
    double *outRel_tuples_double = (double *)outRel->tuples;

    for (i = 0; i < inRel->num_tuples; i += TILE_SIZE) {
        maa_const(i * 2, reg0);
        maa_stream_load<int>(inRel_tuples_int, reg0, reg1, regConst2, tile0);
        maa_alu_scalar<int>(tile0, reg2, tile1, Operation_t::AND_OP);
        // Transfer tile1
        maa_alu_scalar<int>(tile1, reg3, tile2, Operation_t::SHR_OP);
        // private hist
        maa_indirect_rmw_scalar<int>(hist, tile2, regConst1, Operation_t::ADD_OP);
        wait_ready(tile0);
    }
    wait_ready(tile2);

    offset = 0;
    /* determine the start and end of each cluster depending on the counts. */
    for (i = 0; i < fanOut; i++) {
        /* dst[i]      = outRel->tuples + offset; */
        /* determine the beginning of each partitioning by adding some
           padding to avoid L1 conflict misses during scatter. */
        dst[i] = offset + i * SMALL_PADDING_TUPLES;
        offset += hist[i];
    }

    // /* copy tuples to their corresponding clusters at appropriate offsets */
    // for( i=0; i < inRel->num_tuples; i++ ){
    //     uint32_t idx   = HASH_BIT_MODULO(inRel->tuples[i].key, M, R);
    //     outRel->tuples[ dst[idx] ] = inRel->tuples[i];
    //     ++dst[idx];
    // }

    for (i = 0; i < inRel->num_tuples; i += TILE_SIZE) {
        maa_const(i * 2, reg0);
        maa_const(i, reg5);
        // read from inRel->tuples[i].key, apply HASH_BIT_MODULO, and update hist
        maa_stream_load<int>(inRel_tuples_int, reg0, reg1, regConst2, tile0);
        maa_alu_scalar<int>(tile0, reg2, tile1, Operation_t::AND_OP);
        maa_alu_scalar<int>(tile1, reg3, tile2, Operation_t::SHR_OP);
        // private dst
        maa_indirect_rmw_scalar<int>(dst, tile2, regConst1, Operation_t::ADD_OP, -1, tile3);
        // Transfer tile3
        // read from inRel->tuples[i], use dumped dist to update outRel->tuples
        maa_stream_load<double>(inRel_tuples_double, reg5, reg4, regConst1, tile4);
        // shared outRel_tuples_double
        maa_indirect_store_vector<double>(outRel_tuples_double, tile3, tile4);
        wait_ready(tile0);
        wait_ready(tile4);
    }
    DEBUGMSG(1, "radix_cluster_maa ended: tid=%d\n", my_tid);
}
#endif

/** 
 * Radix clustering algorithm which does not put padding in between
 * clusters. This is used only by single threaded radix join implementation RJ.
 * 
 * @param outRel 
 * @param inRel 
 * @param hist 
 * @param R 
 * @param D 
 */
void radix_cluster_nopadding(relation_t *outRel, relation_t *inRel, int R, int D) {
    tuple_t **dst;
    tuple_t *input;
    /* tuple_t ** dst_end; */
    uint32_t *tuples_per_cluster;
    uint32_t i;
    uint32_t offset;
    const uint32_t M = ((1 << D) - 1) << R;
    const uint32_t fanOut = 1 << D;
    const uint32_t ntuples = inRel->num_tuples;

    tuples_per_cluster = (uint32_t *)calloc(fanOut, sizeof(uint32_t));
    /* the following are fixed size when D is same for all the passes,
       and can be re-used from call to call. Allocating in this function 
       just in case D differs from call to call. */
    dst = (tuple_t **)malloc(sizeof(tuple_t *) * fanOut);
    /* dst_end = (tuple_t**)malloc(sizeof(tuple_t*)*fanOut); */

    input = inRel->tuples;
    /* count tuples per cluster */
    for (i = 0; i < ntuples; i++) {
        uint32_t idx = (uint32_t)(HASH_BIT_MODULO(input->key, M, R));
        tuples_per_cluster[idx]++;
        input++;
    }

    offset = 0;
    /* determine the start and end of each cluster depending on the counts. */
    for (i = 0; i < fanOut; i++) {
        dst[i] = outRel->tuples + offset;
        offset += tuples_per_cluster[i];
        /* dst_end[i]  = outRel->tuples + offset; */
    }

    input = inRel->tuples;
    /* copy tuples to their corresponding clusters at appropriate offsets */
    for (i = 0; i < ntuples; i++) {
        uint32_t idx = (uint32_t)(HASH_BIT_MODULO(input->key, M, R));
        *dst[idx] = *input;
        ++dst[idx];
        input++;
        /* we pre-compute the start and end of each cluster, so the following
           check is unnecessary */
        /* if(++dst[idx] >= dst_end[idx]) */
        /*     REALLOCATE(dst[idx], dst_end[idx]); */
    }

    /* clean up temp */
    /* free(dst_end); */
    free(dst);
    free(tuples_per_cluster);
}

/** 
 * This function implements the radix clustering of a given input
 * relations. The relations to be clustered are defined in task_t and after
 * clustering, each partition pair is added to the join_queue to be joined.
 * 
 * @param task description of the relation to be partitioned
 * @param join_queue task queue to add join tasks after clustering
 */
void serial_radix_partition(task_t *const task,
                            task_queue_t *join_queue,
                            const int R, const int D, arg_t *arg) {

    int32_t tid = omp_get_thread_num();
    DEBUGMSG(1, "serial_radix_partition started, tid: %d\n", tid);

    int i;
    uint32_t offsetR = 0, offsetS = 0;
    const int fanOut = 1 << D; /*(NUM_RADIX_BITS / NUM_PASSES);*/
    const size_t fo_size = (fanOut + 1) * sizeof(int32_t);

    int32_t *outputR = task->outR[tid];
    int32_t *outputS = task->outS[tid];
    int32_t *dst = task->dst[tid];
    memset(outputR, 0, fo_size);
    memset(outputS, 0, fo_size);

    DEBUGMSG(1, "allocated, OR: %p, OS: %p, DST: %p, tid: %d\n", outputR, outputS, dst, tid);

#ifdef MAA
    radix_cluster_maa(&task->tmpR, &task->relR, outputR, dst, R, D, arg);
#else
    radix_cluster(&task->tmpR, &task->relR, outputR, dst, R, D, arg);
#endif

/* memset(outputS, 0, fanOut * sizeof(int32_t)); */
#ifdef MAA
    radix_cluster_maa(&task->tmpS, &task->relS, outputS, dst, R, D, arg);
#else
    radix_cluster(&task->tmpS, &task->relS, outputS, dst, R, D, arg);
#endif

    /* task_t t; */
    for (i = 0; i < fanOut; i++) {
        DEBUGMSG(1, "outputR[%d]: %d, outputS[%d]: %d, tid: %d\n", i, outputR[i], i, outputS[i], tid);
        if (outputR[i] > 0 && outputS[i] > 0) {
            task_t *t = task_queue_get_slot_atomic(join_queue);
            t->relR.num_tuples = outputR[i];
            t->relR.tuples = task->tmpR.tuples + offsetR + i * SMALL_PADDING_TUPLES;
            t->tmpR.tuples = task->relR.tuples + offsetR + i * SMALL_PADDING_TUPLES;
            offsetR += outputR[i];

            t->relS.num_tuples = outputS[i];
            t->relS.tuples = task->tmpS.tuples + offsetS + i * SMALL_PADDING_TUPLES;
            t->tmpS.tuples = task->relS.tuples + offsetS + i * SMALL_PADDING_TUPLES;
            offsetS += outputS[i];

            /* task_queue_copy_atomic(join_queue, &t); */
            task_queue_add_atomic(join_queue, t);
        } else {
            offsetR += outputR[i];
            offsetS += outputS[i];
        }
    }
}

/** 
 * This function implements the parallel radix partitioning of a given input
 * relation. Parallel partitioning is done by histogram-based relation
 * re-ordering as described by Kim et al. Parallel partitioning method is
 * commonly used by all parallel radix join algorithms.
 * 
 * @param part description of the relation to be partitioned
 */
void parallel_radix_partition(part_t *const part) {
    const tuple_t *rel = part->rel;
    int32_t **hist = part->hist;
    int32_t *output = part->output;

    int my_tid = omp_get_thread_num();
    const uint32_t nthreads = part->thrargs->nthreads;
    const uint32_t num_tuples = part->num_tuples;

    const int32_t R = part->R;
    const int32_t D = part->D;
    const uint32_t fanOut = 1 << D;
    const uint32_t MASK = (fanOut - 1) << R;
    const uint32_t padding = part->padding;
    int32_t *dst = part->dst;

    int32_t sum = 0;
    uint32_t i, j;

    /* compute local histogram for the assigned region of rel */
    /* compute histogram */
    int32_t *my_hist = hist[my_tid];

    for (i = 0; i < num_tuples; i++) {
        uint32_t idx = HASH_BIT_MODULO(rel[i].key, MASK, R);
        my_hist[idx]++;
    }

    /* compute local prefix sum on hist */
    for (i = 0; i < fanOut; i++) {
        sum += my_hist[i];
        my_hist[i] = sum;
    }

/* wait at a barrier until each thread complete histograms */
#pragma omp barrier

    /* determine the start and end of each cluster */
    for (i = 0; i < my_tid; i++) {
        for (j = 0; j < fanOut; j++)
            output[j] += hist[i][j];
    }
    for (i = my_tid; i < nthreads; i++) {
        for (j = 1; j < fanOut; j++)
            output[j] += hist[i][j - 1];
    }

    for (i = 0; i < fanOut; i++) {
        output[i] += i * padding; //PADDING_TUPLES;
        dst[i] = output[i];
    }
    output[fanOut] = part->total_tuples + fanOut * padding; //PADDING_TUPLES;

    tuple_t *tmp = part->tmp;
    /* Copy tuples to their corresponding clusters */
    for (i = 0; i < num_tuples; i++) {
        uint32_t idx = HASH_BIT_MODULO(rel[i].key, MASK, R);
        tmp[dst[idx]] = rel[i];
        ++dst[idx];
    }
}

#ifdef MAA
/** 
 * This function implements the parallel radix partitioning of a given input
 * relation. Parallel partitioning is done by histogram-based relation
 * re-ordering as described by Kim et al. Parallel partitioning method is
 * commonly used by all parallel radix join algorithms.
 * 
 * @param part description of the relation to be partitioned
 */
void parallel_radix_partition_maa(part_t *const part) {
    const tuple_t *rel = part->rel;
    int32_t **hist = part->hist;
    int32_t *output = part->output;

    const uint32_t my_tid = omp_get_thread_num();
    const uint32_t nthreads = part->thrargs->nthreads;
    const uint32_t num_tuples = part->num_tuples;
    const int32_t R = part->R;
    const int32_t D = part->D;
    const uint32_t fanOut = 1 << D;
    const uint32_t MASK = (fanOut - 1) << R;
    const uint32_t padding = part->padding;
    arg_t *args = part->thrargs;
    int reg0, reg1, reg2, reg3, reg5, regConst2, regConst1, reg4;
    int tile0, tile1, tile2, tile3, tile4;
    reg0 = args->reg0;
    reg1 = args->reg1;
    reg2 = args->reg2;
    reg3 = args->reg3;
    reg5 = args->reg5;
    regConst2 = args->regConst2;
    regConst1 = args->regConst1;
    reg4 = args->reg4;
    tile0 = args->tile0;
    tile1 = args->tile1;
    tile2 = args->tile2;
    tile3 = args->tile3;
    tile4 = args->tile4;

    int32_t sum = 0;
    uint32_t i, j;
    int rv;

    int32_t *dst = part->dst;

    /* compute local histogram for the assigned region of rel */
    /* compute histogram */
    int32_t *my_hist = hist[my_tid];

    maa_const<int>(num_tuples * 2, reg1);
    maa_const<int>(num_tuples, reg4);
    maa_const<int>(MASK, reg2);
    maa_const<int>(R, reg3);
    int *relKeyStart = (int *)&(rel[0].key);
    // for(i = 0; i < num_tuples; i++) {
    //     uint32_t idx = HASH_BIT_MODULO(rel[i].key, MASK, R);
    //     my_hist[idx] ++;
    // }
    for (i = 0; i < num_tuples; i += TILE_SIZE) {
        maa_const(i * 2, reg0);
        maa_stream_load<int>(relKeyStart, reg0, reg1, regConst2, tile0);
        maa_alu_scalar<int>(tile0, reg2, tile1, Operation_t::AND_OP);
        // Transfer tile1
        maa_alu_scalar<int>(tile1, reg3, tile2, Operation_t::SHR_OP);
        // private my_hist
        maa_indirect_rmw_scalar<int>(my_hist, tile2, regConst1, Operation_t::ADD_OP);
        wait_ready(tile0);
    }
    wait_ready(tile2);

    /* compute local prefix sum on hist */
    for (i = 0; i < fanOut; i++) {
        sum += my_hist[i];
        my_hist[i] = sum;
    }

    /* wait at a barrier until each thread complete histograms */
#pragma omp barrier

    /* determine the start and end of each cluster */
    for (i = 0; i < my_tid; i++) {
        for (j = 0; j < fanOut; j++)
            output[j] += hist[i][j];
    }
    for (i = my_tid; i < nthreads; i++) {
        for (j = 1; j < fanOut; j++)
            output[j] += hist[i][j - 1];
    }

    for (i = 0; i < fanOut; i++) {
        output[i] += i * padding; //PADDING_TUPLES;
        dst[i] = output[i];
    }
    output[fanOut] = part->total_tuples + fanOut * padding; //PADDING_TUPLES;

    tuple_t *tmp = part->tmp;

    double *tmp_double = (double *)tmp;
    double *rel_double = (double *)rel;
    assert(sizeof(tuple_t) == sizeof(double));
    assert((void *)tmp == (void *)tmp_double);

    /* Copy tuples to their corresponding clusters */
    // for(i = 0; i < num_tuples; i++ ){
    //     uint32_t idx = HASH_BIT_MODULO(rel[i].key, MASK, R);
    //     tmp[dst[idx]] = rel[i];
    //     ++dst[idx];
    // }
    for (i = 0; i < num_tuples; i += TILE_SIZE) {
        maa_const(i * 2, reg0);
        maa_const(i, reg5);
        // read from inRel->tuples[i].key, apply HASH_BIT_MODULO, and update hist
        maa_stream_load<int>(relKeyStart, reg0, reg1, regConst2, tile0);
        maa_alu_scalar<int>(tile0, reg2, tile1, Operation_t::AND_OP);
        maa_alu_scalar<int>(tile1, reg3, tile3, Operation_t::SHR_OP);
        // shared dst
        maa_indirect_rmw_scalar<int>(dst, tile3, regConst1, Operation_t::ADD_OP, -1, tile2);
        // Transfer tile2
        // read from inRel->tuples[i], use dumped dist to update outRel->tuples
        // shared rel_double
        maa_stream_load<double>(rel_double, reg5, reg4, regConst1, tile4);
        maa_indirect_store_vector<double>(tmp_double, tile2, tile4);
        wait_ready(tile4);
        wait_ready(tile0);
    }
}
#endif
/** 
 * @defgroup SoftwareManagedBuffer Optimized Partitioning Using SW-buffers
 * @{
 */
typedef union {
    struct {
        tuple_t tuples[CACHE_LINE_SIZE / sizeof(tuple_t)];
    } tuples;
    struct {
        tuple_t tuples[CACHE_LINE_SIZE / sizeof(tuple_t) - 1];
        int32_t slot;
    } data;
} cacheline_t;

#define TUPLESPERCACHELINE (CACHE_LINE_SIZE / sizeof(tuple_t))

/** 
 * Makes a non-temporal write of 64 bytes from src to dst.
 * Uses vectorized non-temporal stores if available, falls
 * back to assignment copy.
 *
 * @param dst
 * @param src
 * 
 * @return 
 */
static inline void
store_nontemp_64B(void *dst, void *src) {
    /* just copy with assignment */
    *(cacheline_t *)dst = *(cacheline_t *)src;
}

/** 
 * This function implements the parallel radix partitioning of a given input
 * relation. Parallel partitioning is done by histogram-based relation
 * re-ordering as described by Kim et al. Parallel partitioning method is
 * commonly used by all parallel radix join algorithms. However this
 * implementation is further optimized to benefit from write-combining and
 * non-temporal writes.
 * 
 * @param part description of the relation to be partitioned
 */
void parallel_radix_partition_optimized(part_t *const part) {
    const tuple_t *rel = part->rel;
    int32_t **hist = part->hist;
    int32_t *output = part->output;

    const uint32_t my_tid = omp_get_thread_num();
    const uint32_t nthreads = part->thrargs->nthreads;
    const uint32_t num_tuples = part->num_tuples;

    const int32_t R = part->R;
    const int32_t D = part->D;
    const uint32_t fanOut = 1 << D;
    const uint32_t MASK = (fanOut - 1) << R;
    const uint32_t padding = part->padding;

    int32_t sum = 0;
    uint32_t i, j;

    /* compute local histogram for the assigned region of rel */
    /* compute histogram */
    int32_t *my_hist = hist[my_tid];

    for (i = 0; i < num_tuples; i++) {
        uint32_t idx = HASH_BIT_MODULO(rel[i].key, MASK, R);
        my_hist[idx]++;
    }

    /* compute local prefix sum on hist */
    for (i = 0; i < fanOut; i++) {
        sum += my_hist[i];
        my_hist[i] = sum;
    }

/* wait at a barrier until each thread complete histograms */
#pragma omp barrier

    /* determine the start and end of each cluster */
    for (i = 0; i < my_tid; i++) {
        for (j = 0; j < fanOut; j++)
            output[j] += hist[i][j];
    }
    for (i = my_tid; i < nthreads; i++) {
        for (j = 1; j < fanOut; j++)
            output[j] += hist[i][j - 1];
    }

    /* uint32_t pre; /\* nr of tuples to cache-alignment *\/ */
    tuple_t *tmp = part->tmp;
    /* software write-combining buffer */
    cacheline_t buffer[fanOut] __attribute__((aligned(CACHE_LINE_SIZE)));

    for (i = 0; i < fanOut; i++) {
        uint32_t off = output[i] + i * padding;
        /* pre        = (off + TUPLESPERCACHELINE) & ~(TUPLESPERCACHELINE-1); */
        /* pre       -= off; */
        output[i] = off;
        buffer[i].data.slot = off;
    }
    output[fanOut] = part->total_tuples + fanOut * padding;

    /* Copy tuples to their corresponding clusters */
    for (i = 0; i < num_tuples; i++) {
        uint32_t idx = HASH_BIT_MODULO(rel[i].key, MASK, R);
        uint32_t slot = buffer[idx].data.slot;
        tuple_t *tup = (tuple_t *)(buffer + idx);
        uint32_t slotMod = (slot) & (TUPLESPERCACHELINE - 1);
        tup[slotMod] = rel[i];

        if (slotMod == (TUPLESPERCACHELINE - 1)) {
            /* write out 64-Bytes with non-temporal store */
            store_nontemp_64B((tmp + slot - (TUPLESPERCACHELINE - 1)), (buffer + idx));
            /* writes += TUPLESPERCACHELINE; */
        }

        buffer[idx].data.slot = slot + 1;
    }
    /* _mm_sfence (); */

    /* write out the remainders in the buffer */
    for (i = 0; i < fanOut; i++) {
        uint32_t slot = buffer[i].data.slot;
        uint32_t sz = (slot) & (TUPLESPERCACHELINE - 1);
        slot -= sz;
        for (uint32_t j = 0; j < sz; j++) {
            tmp[slot] = buffer[i].data.tuples[j];
            slot++;
        }
    }
}

/** @} */

/** 
 * The main thread of parallel radix join. It does partitioning in parallel with
 * other threads and during the join phase, picks up join tasks from the task
 * queue and calls appropriate JoinFunction to compute the join task.
 * 
 * @param param 
 * 
 * @return 
 */
void *prj_thread(void *param) {
    arg_t *args = (arg_t *)param;
#ifdef MAA
#pragma omp critical
    {
        args->reg0 = get_new_reg<int>();
        args->reg1 = get_new_reg<int>();
        args->reg2 = get_new_reg<int>();
        args->reg3 = get_new_reg<int>();
        args->reg4 = get_new_reg<int>();
        args->reg5 = get_new_reg<int>();
        args->regConst2 = get_new_reg<int>(2);
        args->regConst1 = get_new_reg<int>(1);
        args->tile0 = get_new_tile<int>();
        args->tile1 = get_new_tile<int>();
        args->tile2 = get_new_tile<int>();
        args->tile3 = get_new_tile<int>();
        args->tile4 = get_new_tile<int>();
        args->tile5 = get_new_tile<int>();
        args->tile6 = get_new_tile<int>();
        args->tile7 = get_new_tile<int>();
        DEBUGMSG(1, "Thread %d: reg0 = %d, reg1 = %d, reg2 = %d, reg3 = %d, reg4: %d, reg5: %d, regConst2 = %d, regConst1 = %d, tile0 = %d, tile1 = %d, tile2 = %d, tile3 = %d, tile4 = %d, tile5 = %d, tile6 = %d, tile7 = %d\n", omp_get_thread_num(), args->reg0, args->reg1, args->reg2, args->reg3, args->reg4, args->reg5, args->regConst2, args->regConst1, args->tile0, args->tile1, args->tile2, args->tile3, args->tile4, args->tile5, args->tile6, args->tile7);
    }
#endif

    int32_t my_tid = omp_get_thread_num();

    const int fanOut = 1 << (NUM_RADIX_BITS / NUM_PASSES);
    const int R = (NUM_RADIX_BITS / NUM_PASSES);
    const int D = (NUM_RADIX_BITS - (NUM_RADIX_BITS / NUM_PASSES));
    const int thresh1 = MAX((1 << D), (1 << R)) * THRESHOLD1(args->nthreads);

    uint64_t results = 0;
    int i;

    part_t part;
    task_t *task;
    task_queue_t *part_queue;
    task_queue_t *join_queue;
#ifdef SKEW_HANDLING
    task_queue_t *skew_queue;
#endif

    int32_t *outputR = args->outR[my_tid];
    int32_t *outputS = args->outS[my_tid];
    memset(outputR, 0, (fanOut + 1) * sizeof(int32_t));
    memset(outputS, 0, (fanOut + 1) * sizeof(int32_t));

    part_queue = args->part_queue;
    join_queue = args->join_queue;
#ifdef SKEW_HANDLING
    skew_queue = args->skew_queue;
#endif

    /* in the first pass, partitioning is done together by all threads */

    args->parts_processed = 0;

    /* wait at a barrier until each thread starts and then start the timer */
#pragma omp barrier

    /********** 1st pass of multi-pass partitioning ************/
    part.R = 0;
    part.D = NUM_RADIX_BITS / NUM_PASSES;
    part.thrargs = args;
    part.padding = PADDING_TUPLES;
    part.dst = args->dst[my_tid];

    /* 1. partitioning for relation R */
    part.rel = args->relR;
    part.tmp = args->tmpR;
    part.hist = args->histR;
    part.output = outputR;
    part.num_tuples = args->numR;
    part.total_tuples = args->totalR;
    part.relidx = 0;

#ifdef USE_SWWC_OPTIMIZED_PART
    parallel_radix_partition_optimized(&part);
#elif defined(MAA)
    parallel_radix_partition_maa(&part);
#else
    parallel_radix_partition(&part);
#endif

    /* 2. partitioning for relation S */
    part.rel = args->relS;
    part.tmp = args->tmpS;
    part.hist = args->histS;
    part.output = outputS;
    part.num_tuples = args->numS;
    part.total_tuples = args->totalS;
    part.relidx = 1;

#ifdef USE_SWWC_OPTIMIZED_PART
    parallel_radix_partition_optimized(&part);
#elif defined(MAA)
    parallel_radix_partition_maa(&part);
#else
    parallel_radix_partition(&part);
#endif

/* wait at a barrier until each thread copies out */
#pragma omp barrier

    /********** end of 1st partitioning phase ******************/

    /* 3. first thread creates partitioning tasks for 2nd pass */
    if (my_tid == 0) {
        for (i = 0; i < fanOut; i++) {
            int32_t ntupR = outputR[i + 1] - outputR[i] - PADDING_TUPLES;
            int32_t ntupS = outputS[i + 1] - outputS[i] - PADDING_TUPLES;

#ifdef SKEW_HANDLING
            if (ntupR > thresh1 || ntupS > thresh1) {
                DEBUGMSG(1, "Adding to skew_queue= R:%d, S:%d\n", ntupR, ntupS);

                task_t *t = task_queue_get_slot(skew_queue);
                t->dst = args->dst;

                t->relR.num_tuples = t->tmpR.num_tuples = ntupR;
                t->relR.tuples = args->tmpR + outputR[i];
                t->tmpR.tuples = args->relR + outputR[i];
                t->outR = args->outR;

                t->relS.num_tuples = t->tmpS.num_tuples = ntupS;
                t->relS.tuples = args->tmpS + outputS[i];
                t->tmpS.tuples = args->relS + outputS[i];
                t->outS = args->outS;

                task_queue_add(skew_queue, t);
            } else
#endif

                if (ntupR > 0 && ntupS > 0) {
                task_t *t = task_queue_get_slot(part_queue);
                t->dst = args->dst;

                t->relR.num_tuples = t->tmpR.num_tuples = ntupR;
                t->relR.tuples = args->tmpR + outputR[i];
                t->tmpR.tuples = args->relR + outputR[i];
                t->outR = args->outR;

                t->relS.num_tuples = t->tmpS.num_tuples = ntupS;
                t->relS.tuples = args->tmpS + outputS[i];
                t->tmpS.tuples = args->relS + outputS[i];
                t->outS = args->outS;

                task_queue_add(part_queue, t);
            }
        }

        /* debug partitioning task queue */
        DEBUGMSG(1, "Pass-2: # partitioning tasks = %d\n", part_queue->count);
    }

    /* wait at a barrier until first thread adds all partitioning tasks */
#pragma omp barrier

    /************ 2nd pass of multi-pass partitioning ********************/
    /* 4. now each thread further partitions and add to join task queue **/

#if NUM_PASSES == 1
    /* If the partitioning is single pass we directly add tasks from pass-1 */
    task_queue_t *swap = join_queue;
    join_queue = part_queue;
    /* part_queue is used as a temporary queue for handling skewed parts */
    part_queue = swap;

#elif NUM_PASSES == 2

    while ((task = task_queue_get_atomic(part_queue))) {
        serial_radix_partition(task, join_queue, R, D, args);
    }

#else
#warning Only 2-pass partitioning is implemented, set NUM_PASSES to 2!
#endif

#ifdef SKEW_HANDLING
    /* Partitioning pass-2 for skewed relations */
    part.R = R;
    part.D = D;
    part.thrargs = args;
    part.padding = SMALL_PADDING_TUPLES;

    while (1) {
        if (my_tid == 0) {
            *args->skewtask = task_queue_get_atomic(skew_queue);
        }
#pragma omp barrier
        if (*args->skewtask == NULL)
            break;

        DEBUGMSG(1, "Got skew task = R: %d, S: %d\n",
                 (*args->skewtask)->relR.num_tuples,
                 (*args->skewtask)->relS.num_tuples);

        int32_t numperthr = (*args->skewtask)->relR.num_tuples / args->nthreads;
        const int fanOut2 = (1 << D);

        memset(outputR, 0, (fanOut2 + 1) * sizeof(int32_t));
        memset(outputS, 0, (fanOut2 + 1) * sizeof(int32_t));
        memset(args->histR[my_tid], 0, (fanOut2 + 1) * sizeof(int32_t));
        memset(args->histS[my_tid], 0, (fanOut2 + 1) * sizeof(int32_t));

/* wait until each thread allocates memory */
#pragma omp barrier

        /* 1. partitioning for relation R */
        part.rel = (*args->skewtask)->relR.tuples + my_tid * numperthr;
        part.tmp = (*args->skewtask)->tmpR.tuples;
        part.hist = args->histR;
        part.output = outputR;
        part.num_tuples = (my_tid == (args->nthreads - 1)) ? ((*args->skewtask)->relR.num_tuples - my_tid * numperthr)
                                                           : numperthr;
        part.total_tuples = (*args->skewtask)->relR.num_tuples;
        part.relidx = 2; /* meaning this is pass-2, no syncstats */
        parallel_radix_partition(&part);

        numperthr = (*args->skewtask)->relS.num_tuples / args->nthreads;
        /* 2. partitioning for relation S */
        part.rel = (*args->skewtask)->relS.tuples + my_tid * numperthr;
        part.tmp = (*args->skewtask)->tmpS.tuples;
        part.hist = args->histS;
        part.output = outputS;
        part.num_tuples = (my_tid == (args->nthreads - 1)) ? ((*args->skewtask)->relS.num_tuples - my_tid * numperthr)
                                                           : numperthr;
        part.total_tuples = (*args->skewtask)->relS.num_tuples;
        part.relidx = 2; /* meaning this is pass-2, no syncstats */
        parallel_radix_partition(&part);

/* wait at a barrier until each thread copies out */
#pragma omp barrier

        /* first thread adds join tasks */
        if (my_tid == 0) {
            const int THR1 = THRESHOLD1(args->nthreads);

            for (i = 0; i < fanOut2; i++) {
                int32_t ntupR = outputR[i + 1] - outputR[i] - SMALL_PADDING_TUPLES;
                int32_t ntupS = outputS[i + 1] - outputS[i] - SMALL_PADDING_TUPLES;
                if (ntupR > THR1 || ntupS > THR1) {

                    DEBUGMSG(1, "Large join task = R: %d, S: %d\n", ntupR, ntupS);

                    /* use part_queue temporarily */
                    for (int k = 0; k < args->nthreads; k++) {
                        int ns = (k == args->nthreads - 1)
                                     ? (ntupS - k * (ntupS / args->nthreads))
                                     : (ntupS / args->nthreads);
                        task_t *t = task_queue_get_slot(part_queue);

                        t->relR.num_tuples = t->tmpR.num_tuples = ntupR;
                        t->relR.tuples = (*args->skewtask)->tmpR.tuples + outputR[i];
                        t->relR.tuples_start = (*args->skewtask)->tmpR.tuples_start;
                        t->relR.tuples_end = (*args->skewtask)->tmpR.tuples_end;
                        t->tmpR.tuples = (*args->skewtask)->relR.tuples + outputR[i];
                        t->tmpR.tuples_start = (*args->skewtask)->relR.tuples_start;
                        t->tmpR.tuples_end = (*args->skewtask)->relR.tuples_end;

                        t->relS.num_tuples = t->tmpS.num_tuples = ns;                //ntupS;
                        t->relS.tuples = (*args->skewtask)->tmpS.tuples + outputS[i] //;
                                         + k * (ntupS / args->nthreads);
                        t->relS.tuples_start = (*args->skewtask)->tmpS.tuples_start;
                        t->relS.tuples_end = (*args->skewtask)->tmpS.tuples_end;
                        t->tmpS.tuples = (*args->skewtask)->relS.tuples + outputS[i] //;
                                         + k * (ntupS / args->nthreads);
                        t->tmpS.tuples_start = (*args->skewtask)->relS.tuples_start;
                        t->tmpS.tuples_end = (*args->skewtask)->relS.tuples_end;

                        task_queue_add(part_queue, t);
                    }
                } else if (ntupR > 0 && ntupS > 0) {
                    task_t *t = task_queue_get_slot(join_queue);

                    t->relR.num_tuples = t->tmpR.num_tuples = ntupR;
                    t->relR.tuples = (*args->skewtask)->tmpR.tuples + outputR[i];
                    t->relR.tuples_start = (*args->skewtask)->tmpR.tuples_start;
                    t->relR.tuples_end = (*args->skewtask)->tmpR.tuples_end;
                    t->tmpR.tuples = (*args->skewtask)->relR.tuples + outputR[i];
                    t->tmpR.tuples_start = (*args->skewtask)->relR.tuples_start;
                    t->tmpR.tuples_end = (*args->skewtask)->relR.tuples_end;

                    t->relS.num_tuples = t->tmpS.num_tuples = ntupS;
                    t->relS.tuples = (*args->skewtask)->tmpS.tuples + outputS[i];
                    t->relS.tuples_start = (*args->skewtask)->tmpS.tuples_start;
                    t->relS.tuples_end = (*args->skewtask)->tmpS.tuples_end;
                    t->tmpS.tuples = (*args->skewtask)->relS.tuples + outputS[i];
                    t->tmpS.tuples_start = (*args->skewtask)->relS.tuples_start;
                    t->tmpS.tuples_end = (*args->skewtask)->relS.tuples_end;

                    task_queue_add(join_queue, t);

                    DEBUGMSG(1, "Join added = R: %d, S: %d\n",
                             t->relR.num_tuples, t->relS.num_tuples);
                }
            }
        }
    }

    /* add large join tasks in part_queue to the front of the join queue */
    if (my_tid == 0) {
        while ((task = task_queue_get_atomic(part_queue)))
            task_queue_add(join_queue, task);
    }

#endif

/* wait at a barrier until all threads add all join tasks */
#pragma omp barrier

    DEBUGMSG((my_tid == 0), "Number of join tasks = %d\n", join_queue->count);

    while ((task = task_queue_get_atomic(join_queue))) {
        /* do the actual join. join method differs for different algorithms,
           i.e. bucket chaining, histogram-based, histogram-based with simd &
           prefetching  */
        results += args->join_function(&task->relR, &task->relS, &task->tmpR, args);

        args->parts_processed++;
    }

    args->result = results;

    return 0;
}

/**
 * The template function for different joins: Basically each parallel radix join
 * has a initialization step, partitioning step and build-probe steps. All our 
 * parallel radix implementations have exactly the same initialization and 
 * partitioning steps. Difference is only in the build-probe step. Here are all 
 * the parallel radix join implemetations and their Join (build-probe) functions:
 *
 * - PRO,  Parallel Radix Join Optimized --> bucket_chaining_join()
 * - PRH,  Parallel Radix Join Histogram-based --> histogram_join()
 * - PRHO, Parallel Radix Histogram-based Optimized -> histogram_optimized_join()
 */
int64_t
join_init_run(relation_t *relR, relation_t *relS, JoinFunction jf, int nthreads) {
    int i;
    arg_t *args = new arg_t[nthreads];

    int32_t **histR, **histS, **outR, **outS, **dst;
    void **histR_start, **histS_start;
    void **histR_end, **histS_end;
    tuple_t *tmpRelR, *tmpRelS;
    int32_t numperthr[2];
    int64_t result = 0;

    task_queue_t *part_queue, *join_queue;
#ifdef SKEW_HANDLING
    task_queue_t *skew_queue;
    task_t *skewtask = NULL;
    skew_queue = task_queue_init(FANOUT_PASS1);
#endif
    part_queue = task_queue_init(FANOUT_PASS1);
    join_queue = task_queue_init((1 << NUM_RADIX_BITS));

    /* allocate temporary space for partitioning */
    tmpRelR = (tuple_t *)malloc(relR->num_tuples * sizeof(tuple_t) + RELATION_PADDING);
    tmpRelS = (tuple_t *)malloc(relS->num_tuples * sizeof(tuple_t) + RELATION_PADDING);

    memset(tmpRelR, 0, relR->num_tuples * sizeof(tuple_t) + RELATION_PADDING);
    memset(tmpRelS, 0, relS->num_tuples * sizeof(tuple_t) + RELATION_PADDING);

    MALLOC_CHECK((tmpRelR && tmpRelS));
    assert(numalocalize == false);

    const int fanOut = 1 << (NUM_RADIX_BITS / NUM_PASSES);
    const int D = (NUM_RADIX_BITS - (NUM_RADIX_BITS / NUM_PASSES));
    const int fanOut2 = (1 << D);
    const int maxFanOut = MAX(fanOut, fanOut2);

    histR = (int32_t **)malloc(nthreads * sizeof(int32_t *));
    histS = (int32_t **)malloc(nthreads * sizeof(int32_t *));
    outR = (int32_t **)malloc(nthreads * sizeof(int32_t *));
    outS = (int32_t **)malloc(nthreads * sizeof(int32_t *));
    dst = (int32_t **)malloc(nthreads * sizeof(int32_t *));
    MALLOC_CHECK((histR && histS && outR && outS && dst));
    for (int i = 0; i < nthreads; i++) {
        histR[i] = (int32_t *)calloc(maxFanOut + 1, sizeof(int32_t));
        histS[i] = (int32_t *)calloc(maxFanOut + 1, sizeof(int32_t));
        outR[i] = (int32_t *)calloc(maxFanOut + 1, sizeof(int32_t));
        outS[i] = (int32_t *)calloc(maxFanOut + 1, sizeof(int32_t));
        dst[i] = (int32_t *)calloc(maxFanOut + 1, sizeof(int32_t));
        MALLOC_CHECK((histR[i] && histS[i] && outR[i] && outS[i] && dst[i]));
    }

#ifdef GEM5
    DEBUGMSG(1, "Checkpoint started\n");
    m5_checkpoint(0, 0);
    DEBUGMSG(1, "Checkpoint ended\n");
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    alloc_MAA();
    init_MAA();

    int reg = 7;
    m5_add_mem_region(relR->tuples_start, relR->tuples_end, reg++);
    m5_add_mem_region(relS->tuples_start, relS->tuples_end, reg++);
    m5_add_mem_region(tmpRelR, tmpRelR + (relR->num_tuples + RELATION_PADDING / sizeof(tuple_t)), reg++);
    m5_add_mem_region(tmpRelS, tmpRelS + (relS->num_tuples + RELATION_PADDING / sizeof(tuple_t)), reg++);
    for (int i = 0; i < nthreads; i++) {
        m5_add_mem_region(histR[i], histR[i] + (maxFanOut + 1), reg++);
        m5_add_mem_region(histS[i], histS[i] + (maxFanOut + 1), reg++);
        m5_add_mem_region(outR[i], outR[i] + (maxFanOut + 1), reg++);
        m5_add_mem_region(outS[i], outS[i] + (maxFanOut + 1), reg++);
        m5_add_mem_region(dst[i], dst[i] + (maxFanOut + 1), reg++);
    }

    /* first assign chunks of relR & relS for each thread */
    numperthr[0] = relR->num_tuples / nthreads;
    numperthr[1] = relS->num_tuples / nthreads;
    for (i = 0; i < nthreads; i++) {
        args[i].dst = dst;

        args[i].relR = relR->tuples + i * numperthr[0];
        args[i].tmpR = tmpRelR;
        args[i].histR = histR;
        args[i].outR = outR;

        args[i].relS = relS->tuples + i * numperthr[1];
        args[i].tmpS = tmpRelS;
        args[i].histS = histS;
        args[i].outS = outS;

        args[i].numR = (i == (nthreads - 1)) ? (relR->num_tuples - i * numperthr[0]) : numperthr[0];
        args[i].numS = (i == (nthreads - 1)) ? (relS->num_tuples - i * numperthr[1]) : numperthr[1];
        args[i].totalR = relR->num_tuples;
        args[i].totalS = relS->num_tuples;

        args[i].part_queue = part_queue;
        args[i].join_queue = join_queue;
#ifdef SKEW_HANDLING
        args[i].skew_queue = skew_queue;
        args[i].skewtask = &skewtask;
#endif
        args[i].join_function = jf;
        args[i].nthreads = nthreads;
    }

#pragma omp parallel
    {
        int tid = omp_get_thread_num();
        prj_thread((void *)&args[tid]);
    }

    /* wait for threads to finish */
    for (i = 0; i < nthreads; i++) {
        result += args[i].result;
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
    DEBUGMSG(1, "ROI End!!!\n");
    m5_exit(0);
#endif

    /* clean up */
    for (i = 0; i < nthreads; i++) {
        free(histR[i]);
        free(histS[i]);
        free(outR[i]);
        free(outS[i]);
        free(dst[i]);
    }
    free(histR);
    free(histS);
    free(outR);
    free(outS);
    free(dst);
    task_queue_free(part_queue);
    task_queue_free(join_queue);
#ifdef SKEW_HANDLING
    task_queue_free(skew_queue);
#endif
    free(tmpRelR);
    free(tmpRelS);

    return result;
}

/** \copydoc PRO */
int64_t
PRO(relation_t *relR, relation_t *relS, int nthreads) {
#ifdef MAA
    printf("MAA is enabled\n");
#endif
    return join_init_run(relR, relS, bucket_chaining_join, nthreads);
}

/** \copydoc PRH */
int64_t
PRH(relation_t *relR, relation_t *relS, int nthreads) {
#ifdef MAA
    printf("MAA is enabled\n");
#endif
    int result = join_init_run(relR, relS, histogram_join, nthreads);

    return result;
}

/** \copydoc PRHO */
int64_t
PRHO(relation_t *relR, relation_t *relS, int nthreads) {
    return 0;
}

/** \copydoc RJ */
int64_t
RJ(relation_t *relR, relation_t *relS, int nthreads) {
    int64_t result = 0;
    uint32_t i;
    arg_t args;

    relation_t *outRelR, *outRelS;

    outRelR = (relation_t *)malloc(sizeof(relation_t));
    outRelS = (relation_t *)malloc(sizeof(relation_t));
    MALLOC_CHECK((outRelR && outRelS));

    /* allocate temporary space for partitioning */
    /* TODO: padding problem */
    size_t sz = relR->num_tuples * sizeof(tuple_t) + RELATION_PADDING;
    outRelR->tuples = (tuple_t *)malloc(sz);
    MALLOC_CHECK(outRelR->tuples);
    outRelR->num_tuples = relR->num_tuples;

    sz = relS->num_tuples * sizeof(tuple_t) + RELATION_PADDING;
    outRelS->tuples = (tuple_t *)malloc(sz);
    MALLOC_CHECK(outRelR->tuples);
    outRelS->num_tuples = relS->num_tuples;

    /***** do the multi-pass partitioning *****/
#if NUM_PASSES == 1
    /* apply radix-clustering on relation R for pass-1 */
    radix_cluster_nopadding(outRelR, relR, 0, NUM_RADIX_BITS);
    relR = outRelR;

    /* apply radix-clustering on relation S for pass-1 */
    radix_cluster_nopadding(outRelS, relS, 0, NUM_RADIX_BITS);
    relS = outRelS;

#elif NUM_PASSES == 2
    /* apply radix-clustering on relation R for pass-1 */
    radix_cluster_nopadding(outRelR, relR, 0, NUM_RADIX_BITS / NUM_PASSES);

    /* apply radix-clustering on relation S for pass-1 */
    radix_cluster_nopadding(outRelS, relS, 0, NUM_RADIX_BITS / NUM_PASSES);

    /* apply radix-clustering on relation R for pass-2 */
    radix_cluster_nopadding(relR, outRelR,
                            NUM_RADIX_BITS / NUM_PASSES,
                            NUM_RADIX_BITS - (NUM_RADIX_BITS / NUM_PASSES));

    /* apply radix-clustering on relation S for pass-2 */
    radix_cluster_nopadding(relS, outRelS,
                            NUM_RADIX_BITS / NUM_PASSES,
                            NUM_RADIX_BITS - (NUM_RADIX_BITS / NUM_PASSES));

    /* clean up temporary relations */
    free(outRelR->tuples);
    free(outRelS->tuples);
    free(outRelR);
    free(outRelS);

#else
#error Only 1 or 2 pass partitioning is implemented, change NUM_PASSES!
#endif

    int *R_count_per_cluster = (int *)malloc((1 << NUM_RADIX_BITS) * sizeof(int));
    memset(R_count_per_cluster, 0, (1 << NUM_RADIX_BITS) * sizeof(int));
    int *S_count_per_cluster = (int *)malloc((1 << NUM_RADIX_BITS) * sizeof(int));
    memset(S_count_per_cluster, 0, (1 << NUM_RADIX_BITS) * sizeof(int));
    MALLOC_CHECK((R_count_per_cluster && S_count_per_cluster));

    /* compute number of tuples per cluster */
    for (i = 0; i < relR->num_tuples; i++) {
        uint32_t idx = (relR->tuples[i].key) & ((1 << NUM_RADIX_BITS) - 1);
        R_count_per_cluster[idx]++;
    }
    for (i = 0; i < relS->num_tuples; i++) {
        uint32_t idx = (relS->tuples[i].key) & ((1 << NUM_RADIX_BITS) - 1);
        S_count_per_cluster[idx]++;
    }

    /* build hashtable on inner */
    int r, s; /* start index of next clusters */
    r = s = 0;
    for (i = 0; i < (1 << NUM_RADIX_BITS); i++) {
        relation_t tmpR, tmpS;

        if (R_count_per_cluster[i] > 0 && S_count_per_cluster[i] > 0) {

            tmpR.num_tuples = R_count_per_cluster[i];
            tmpR.tuples = relR->tuples + r;
            r += R_count_per_cluster[i];

            tmpS.num_tuples = S_count_per_cluster[i];
            tmpS.tuples = relS->tuples + s;
            s += S_count_per_cluster[i];

            result += bucket_chaining_join(&tmpR, &tmpS, NULL, &args);
        } else {
            r += R_count_per_cluster[i];
            s += S_count_per_cluster[i];
        }
    }

    /* clean-up temporary buffers */
    free(S_count_per_cluster);
    free(R_count_per_cluster);

#if NUM_PASSES == 1
    /* clean up temporary relations */
    free(outRelR->tuples);
    free(outRelS->tuples);
    free(outRelR);
    free(outRelS);
#endif

    return result;
}

/** @} */