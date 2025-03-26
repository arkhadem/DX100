
#include "MAA.hpp"
#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <random>

#define DELTA 0.001

#if !defined(FUNC) && !defined(GEM5) && !defined(GEM5_MAGIC)
#define FUNC
#endif

#if defined(FUNC)
#include "MAA_functional.hpp"
#elif defined(GEM5)
#include "MAA_gem5.hpp"
#include <gem5/m5ops.h>
#elif defined(GEM5_MAGIC)
#include "MAA_gem5_magic.hpp"
#endif

/*******************************************************************************/
/*******************************************************************************/
/*                                    TESTS                                    */
/*******************************************************************************/
/*******************************************************************************/

#ifdef COMPILER_TEST
void gather_compiler(float *a, float *b, int *idx, int min, int max, const int C);
void scatter_compiler(float *a, float *b, int *idx, int min, int max, const int C);
void rmw_compiler(float *a, float *b, int *idx, int min, int max, const int C);
void gather_scatter_compiler(float *a, float *b, int *idx, int min, int max, const int C);
void gather_rmw_compiler(float *a, float *b, int *idx, int min, int max, const int C);
void gather_rmw_cond_compiler(float *a, float *b, int *idx, int min, int max, const float C, float *cond, const float cond_const);
#endif


void gather(float * __restrict__ a, float * __restrict__ b, int * __restrict__ idx, int min, int max, const float C) {
    std::cout << "starting gather min(" << min << "), max(" << max << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
#pragma omp for simd aligned(a, b: 16) simdlen(4)
    for (int i = min; i < max; i++) {
        a[i] = C * b[idx[i]];
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}
void gather_maa(float * __restrict__ a, float * __restrict__ b, int * __restrict__ idx, int min, int max, const float C) {
    std::cout << "starting gather_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();
    int max_reg_id, min_reg_id, stride_reg_id, idx_tile, b_tile, a_tile;
    float *b_p;
    #pragma omp parallel 
        {
        #pragma omp master 
        {
            // max scalar register
            max_reg_id = get_new_reg<int>(max);
            // min scalar register
            min_reg_id = get_new_reg<int>();
            // stride scalar register
            stride_reg_id = get_new_reg<int>(1);

            // allocate a tile of idx[i]
            idx_tile = get_new_tile<int>();
            // allocate a tile of b[idx[i]]
            b_tile = get_new_tile<float>();
            // get the SPD pointer to the b[idx[i]] tile
            b_p = get_cacheable_tile_pointer<float>(b_tile);
        }
        #pragma omp barrier
        // Here we change a loop from min to max with 1 to
        // a loop from 0 to i_max
        int i_max = max - min;
        for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
            int curr_tile_size = i_max - i_base;
            curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
            int curr_min = i_base + min;
            // i = min
            maa_const(curr_min, min_reg_id);
            // idx[i]
            maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
            // b[idx[i]]
            maa_indirect_load<float>(b, idx_tile, b_tile);
            float *a_s = &(a[min + i_base]);
            // wait until the b tile is fetched to the SPD
            wait_ready(b_tile);
    #ifdef GEM5
            m5_dump_stats(0, 0);
            m5_work_end(0, 0);
            m5_work_begin(0, 0);
            m5_reset_stats(0, 0);
    #endif
    #pragma omp for 
            for (int i_offset = 0; i_offset < curr_tile_size; i_offset++) {
                a_s[i_offset] = C * b_p[i_offset];
            }
    #ifdef GEM5
            m5_dump_stats(0, 0);
            m5_work_end(0, 0);
            m5_work_begin(0, 0);
            m5_reset_stats(0, 0);
    #endif
            }
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
}

