
#include "MAA.hpp"
#include <cassert>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <random>
#include <atomic>

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
void gather_compiler(int32_t *a, int32_t *b, int *idx, int min, int max, const int C);
void scatter_compiler(int32_t *a, int32_t *b, int *idx, int min, int max, const int C);
void rmw_compiler(int32_t *a, int32_t *b, int *idx, int min, int max, const int C);
void gather_scatter_compiler(int32_t *a, int32_t *b, int *idx, int min, int max, const int C);
void gather_rmw_compiler(int32_t *a, int32_t *b, int *idx, int min, int max, const int C);
void gather_rmw_cond_compiler(int32_t *a, int32_t *b, int *idx, int min, int max, const int32_t C, int32_t *cond, const int32_t cond_const);
#endif

void gather_stream(int32_t *a, int32_t *b, int *idx, int min, int max, const int C) {
    std::cout << "starting gather_stream min(" << min << "), max(" << max << ")" << std::endl;
#pragma omp parallel
    {
#ifdef GEM5
#pragma omp single
        {
            m5_work_begin(0, 0);
            m5_reset_stats(0, 0);
        }
#endif
#pragma omp for simd
        for (int i = min; i < max; i++) {
            a[i] = C * b[i];
        }
#ifdef GEM5
#pragma omp single
        {
            m5_dump_stats(0, 0);
            m5_work_end(0, 0);
        }
#endif
    }
}

void gather(int32_t *__restrict__ a, int32_t *__restrict__ b, int *__restrict__ idx, int min, int max, const int32_t C) {
    std::cout << "starting gather min(" << min << "), max(" << max << ")" << std::endl;
#pragma omp parallel
    {
#ifdef GEM5
#pragma omp single
        {

            m5_work_begin(0, 0);
            m5_reset_stats(0, 0);
        }
#endif
#pragma omp for simd aligned(a, b : 16) simdlen(4)
        for (int i = min; i < max; i++) {
            a[i] = C * b[idx[i]];
        }
#ifdef GEM5
#pragma omp single
        {
            m5_dump_stats(0, 0);
            m5_work_end(0, 0);
        }
#endif
    }
}

void gather_maa(int32_t *__restrict__ a, int32_t *__restrict__ b, int *__restrict__ idx, int min, int max, const int32_t C) {
    std::cout << "starting gather_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;
    init_MAA();
    int max_reg_id, min_reg_id, stride_reg_id, idx_tile, b_tile, a_tile;
    int32_t *b_p;
    int32_t *a_s;
#pragma omp parallel
    {
#pragma omp single
        {
#ifdef GEM5
            m5_work_begin(0, 0);
            m5_reset_stats(0, 0);
#endif
            max_reg_id = get_new_reg<int>(max);
            min_reg_id = get_new_reg<int>();
            stride_reg_id = get_new_reg<int>(1);
            idx_tile = get_new_tile<int>();
            b_tile = get_new_tile<int32_t>();
            b_p = get_cacheable_tile_pointer<int32_t>(b_tile);
        }
        int i_max = max - min;
        for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
            int curr_tile_size = i_max - i_base;
            curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
            int curr_min = i_base + min;
#pragma omp single
            {
                maa_const(curr_min, min_reg_id);
                maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
                maa_indirect_load<int32_t>(b, idx_tile, b_tile);
                a_s = &(a[min + i_base]);
                wait_ready(b_tile);
            }
#pragma omp for simd aligned(a_s, b_p : 16) simdlen(4)
            for (int i_offset = 0; i_offset < curr_tile_size; i_offset++) {
                a_s[i_offset] = C * b_p[i_offset];
            }
        }
#ifdef GEM5
#pragma omp single
        {
            m5_dump_stats(0, 0);
            m5_work_end(0, 0);
        }
#endif
    }
}

void gather_full_maa(int32_t *__restrict__ a, int32_t *__restrict__ b, int *__restrict__ idx, int min, int max, const int32_t C) {
    std::cout << "starting gather_full_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;
    init_MAA();

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(1);
    int C_reg_id = get_new_reg<int32_t>(C);
    int idx_tile = get_new_tile<int>();
    int a_tile = get_new_tile<int32_t>();
    int b_tile = get_new_tile<int32_t>();

    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base + min;
        wait_ready(idx_tile);
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        maa_indirect_load<int32_t>(b, idx_tile, b_tile);
        maa_alu_scalar<int32_t>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP);
        maa_stream_store<int32_t>(a, min_reg_id, max_reg_id, stride_reg_id, a_tile);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);

#endif
}

void scatter(int32_t *a, int32_t *b, int *idx, int min, int max, const int C) {
    std::cout << "starting scatter min(" << min << "), max(" << max << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
    for (int i = min; i < max; i++) {
        a[idx[i]] = C * b[i];
        // std::cout << "a[idx[" << i << "](" << idx[i] << ")](" << a[idx[i]] << ") = " << C << " * b[" << i << "](" << b[i] << ")" << std::endl;
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}
void scatter_maa(int32_t *a, int32_t *b, int *idx, int min, int max, const int C) {
    std::cout << "starting scatter_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(1);
    int C_reg_id = get_new_reg<int32_t>(C);

    int idx_tile = get_new_tile<int>();
    int a_tile = get_new_tile<int32_t>();
    int b_tile = get_new_tile<int32_t>();

    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base + min;
        wait_ready(idx_tile);
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        maa_stream_load<int32_t>(b, min_reg_id, max_reg_id, stride_reg_id, b_tile);
        maa_alu_scalar<int32_t>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP);
        maa_indirect_store_vector<int32_t>(a, idx_tile, a_tile);
    }
    wait_ready(a_tile);

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void rmw(int32_t *a, int32_t *b, int *idx, int min, int max, const int C) {
    std::cout << "starting rmw min(" << min << "), max(" << max << ")" << std::endl;
#pragma omp parallel
    {
#ifdef GEM5
#pragma omp single
        {

            m5_work_begin(0, 0);
            m5_reset_stats(0, 0);
        }
#endif
#pragma omp for simd aligned(a, b : 16) simdlen(4)
        for (int i = min; i < max; i++) {
            __atomic_fetch_add(&a[idx[i]], C * b[i], memory_order_relaxed);
        }
#ifdef GEM5
#pragma omp single
        {
            m5_dump_stats(0, 0);
            m5_work_end(0, 0);
        }
#endif
    }
}
void rmw_maa(int32_t *a, int32_t *b, int *idx, int min, int max, const int C) {
    std::cout << "starting rmw_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(1);
    int C_reg_id = get_new_reg<int32_t>(C);

    int idx_tile = get_new_tile<int>();
    int a_tile = get_new_tile<int32_t>();
    int b_tile = get_new_tile<int32_t>();

    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base + min;
        wait_ready(idx_tile);
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        maa_stream_load<int32_t>(b, min_reg_id, max_reg_id, stride_reg_id, b_tile);
        maa_alu_scalar<int32_t>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP);
        maa_indirect_rmw_vector<int32_t>(a, idx_tile, a_tile, Operation_t::ADD_OP);
    }
    wait_ready(a_tile);

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_scatter(int32_t *a, int32_t *b, int *idx, int min, int max, const int C) {
    std::cout << "starting gather_scatter min(" << min << "), max(" << max << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
    for (int i = min; i < max; i++) {
        a[idx[i]] = C * b[idx[i]];
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}
void gather_scatter_maa(int32_t *a, int32_t *b, int *idx, int min, int max, const int C) {
    std::cout << "starting gather_scatter_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(1);
    int C_reg_id = get_new_reg<int32_t>(C);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<int32_t>();
    int a_tile = get_new_tile<int32_t>();

    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base + min;
        wait_ready(idx_tile);
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        maa_indirect_load<int32_t>(b, idx_tile, b_tile);
        maa_alu_scalar<int32_t>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP);
        maa_indirect_store_vector<int32_t>(a, idx_tile, a_tile);
    }
    wait_ready(a_tile);

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw(int32_t *a, int32_t *b, int *idx, int min, int max, const int C) {
    std::cout << "starting gather_rmw min(" << min << "), max(" << max << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
    // TODO: parallelize
    for (int i = min; i < max; i++) {
        a[idx[i]] += C * b[idx[i]];
        // __atomic_fetch_add(&a[idx[i]], C * b[idx[i]], memory_order_relaxed);
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}
void gather_rmw_maa(int32_t *a, int32_t *b, int *idx, int min, int max, const int C) {
    std::cout << "starting maa_gather_rmw min(" << min << "), max(" << max << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(1);
    int C_reg_id = get_new_reg<int32_t>(C);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<int32_t>();
    int a_tile = get_new_tile<int32_t>();

    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base + min;
        wait_ready(idx_tile);
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        maa_indirect_load<int32_t>(b, idx_tile, b_tile);
        maa_alu_scalar<int32_t>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP);
        maa_indirect_rmw_vector<int32_t>(a, idx_tile, a_tile, Operation_t::ADD_OP);
    }
    wait_ready(a_tile);

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}
void gather_rmw_dst(int32_t *a, int32_t *b, int32_t *c, int *idx, int min, int max, const int C) {
    std::cout << "starting gather_rmw min(" << min << "), max(" << max << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
    for (int i = min; i < max; i++) {
        c[i] = a[idx[i]];
        a[idx[i]] += C * b[idx[i]];
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}
void gather_rmw_dst_maa(int32_t *a, int32_t *b, int32_t *c, int *idx, int min, int max, const int C) {
    std::cout << "starting maa_gather_rmw_dst min(" << min << "), max(" << max << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(1);
    int C_reg_id = get_new_reg<int32_t>(C);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<int32_t>();
    int a_tile = get_new_tile<int32_t>();
    int c_tile = get_new_tile<int32_t>();

    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base + min;
        wait_ready(idx_tile);
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        maa_indirect_load<int32_t>(b, idx_tile, b_tile);
        maa_alu_scalar<int32_t>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP);
        maa_indirect_rmw_vector<int32_t>(a, idx_tile, a_tile, Operation_t::ADD_OP, -1, c_tile);
        maa_stream_store<int32_t>(c, min_reg_id, max_reg_id, stride_reg_id, c_tile);
    }
    wait_ready(a_tile);

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_cond(int32_t *a, int32_t *b, int *idx, int min, int max, const int32_t C, int32_t *cond, const int32_t cond_const) {
    std::cout << "starting gather_rmw_cond min(" << min << "), max(" << max << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
    // TODO: parallelize
    for (int i = min; i < max; i++) {
        if (cond_const < cond[i]) {
            a[idx[i]] += C * b[idx[i]];
        }
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}
void gather_rmw_cond_maa(int32_t *a, int32_t *b, int *idx, int min, int max, const int32_t C, int32_t *cond, const int32_t cond_const) {
    std::cout << "starting gather_rmw_cond_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(1);
    int C_reg_id = get_new_reg<int32_t>(C);

    int cond_const_reg_id = get_new_reg<int32_t>(cond_const);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<int32_t>();
    int a_tile = get_new_tile<int32_t>();
    int cond_tile = get_new_tile<int32_t>();
    int cond_res_tile = get_new_tile<uint32_t>();

    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base + min;
        wait_ready(cond_tile);
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int32_t>(cond, min_reg_id, max_reg_id, stride_reg_id, cond_tile);
        maa_alu_scalar<int32_t>(cond_tile, cond_const_reg_id, cond_res_tile, Operation_t::GT_OP);
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile, cond_res_tile);
        maa_indirect_load<int32_t>(b, idx_tile, b_tile, cond_res_tile);
        maa_alu_scalar<int32_t>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP, cond_res_tile);
        maa_indirect_rmw_vector<int32_t>(a, idx_tile, a_tile, Operation_t::ADD_OP, cond_res_tile);
    }
    wait_ready(a_tile);

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_directrangeloop_cond(int *boundaries, int32_t *a, int32_t *b, int *idx, int min, int max, const int32_t C, int32_t *cond, const int32_t cond_const) {
    std::cout << "starting gather_rmw_directrangeloop_cond min(" << min << "), max(" << max << ")" << std::endl;
#pragma omp parallel
    {
#ifdef GEM5
#pragma omp single
        {

            m5_work_begin(0, 0);
            m5_reset_stats(0, 0);
        }
#endif
#pragma omp for simd aligned(a, b : 16) simdlen(4)
        for (int i = min; i < max; i++) {
            for (int j = boundaries[i]; j < boundaries[i + 1]; j++) {
                if (cond_const < cond[j]) {
                    __atomic_fetch_add(&a[idx[j]], C * b[idx[j]], memory_order_relaxed);
                }
            }
        }
#ifdef GEM5
#pragma omp single
        {
            m5_dump_stats(0, 0);
            m5_work_end(0, 0);
        }
#endif
    }
}
void gather_rmw_directrangeloop_cond_maa(int *boundaries, int32_t *a, int32_t *b, int *idx, int min, int max, const int32_t C, int32_t *cond, const int32_t cond_const) {
    std::cout << "starting gather_rmw_directrangeloop_cond_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    int one_reg_id = get_new_reg<int>(1);
    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(1);
    int C_reg_id = get_new_reg<int32_t>(C);

    int cond_const_reg_id = get_new_reg<int32_t>(cond_const);
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<int32_t>();
    int a_tile = get_new_tile<int32_t>();
    int cond_tile = get_new_tile<int32_t>();
    int boundaries0_tile = get_new_tile<int>();
    int boundaries1_tile = get_new_tile<int>();
    int i_tile = get_new_tile<int>();
    int j_tile = get_new_tile<int>();
    int cond_res_tile = get_new_tile<uint32_t>();

    int32_t *b_p = get_cacheable_tile_pointer<int32_t>(b_tile);
    int32_t *a_p = get_cacheable_tile_pointer<int32_t>(a_tile);
    uint32_t *cond_res_p = get_cacheable_tile_pointer<uint32_t>(cond_res_tile);

    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tilei_size = i_max - i_base;
        curr_tilei_size = curr_tilei_size > TILE_SIZE ? TILE_SIZE : curr_tilei_size;
        int curr_min = i_base + min;
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(boundaries, min_reg_id, max_reg_id, stride_reg_id, boundaries0_tile);
        maa_stream_load<int>(&boundaries[1], min_reg_id, max_reg_id, stride_reg_id, boundaries1_tile);

        int curr_tilej_size = 0;
        maa_const<int>(0, last_i_reg_id);
        maa_const<int>(-1, last_j_reg_id);
        do {
            maa_range_loop<int32_t>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile);
            maa_indirect_load<int32_t>(cond, j_tile, cond_tile);
            maa_alu_scalar<int32_t>(cond_tile, cond_const_reg_id, cond_res_tile, Operation_t::GT_OP);
            maa_indirect_load<int>(idx, j_tile, idx_tile, cond_res_tile);
            maa_indirect_load<int32_t>(b, idx_tile, b_tile, cond_res_tile);
            maa_alu_scalar<int32_t>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP, cond_res_tile);
            maa_indirect_rmw_vector<int32_t>(a, idx_tile, a_tile, Operation_t::ADD_OP, cond_res_tile);
            wait_ready(j_tile);
            curr_tilej_size = get_tile_size(j_tile);
        } while (curr_tilej_size > 0);
        wait_ready(a_tile);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_directrangeloop_indircond(int *boundaries, int32_t *a, int32_t *b, int *idx, int min, int max, const int32_t C, int32_t *cond, const int32_t cond_const) {
    std::cout << "starting gather_directrangeloop_indircond min(" << min << "), max(" << max << ")" << std::endl;
#pragma omp parallel
    {
#ifdef GEM5
#pragma omp single
        {

            m5_work_begin(0, 0);
            m5_reset_stats(0, 0);
        }
#endif
#pragma omp for simd aligned(a, b : 16) simdlen(4)
        for (int i = min; i < max; i++) {
            for (int j = boundaries[i]; j < boundaries[i + 1]; j++) {
                if (cond_const < cond[idx[j]]) {
                    a[j] = C * b[idx[j]];
                }
            }
        }
#ifdef GEM5
#pragma omp single
        {
            m5_dump_stats(0, 0);
            m5_work_end(0, 0);
        }
#endif
    }
}
void gather_directrangeloop_indircond_maa(int *boundaries, int32_t *a, int32_t *b, int *idx, int min, int max, const int32_t C, int32_t *cond, const int32_t cond_const) {
    std::cout << "starting gather_directrangeloop_indircond_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    int one_reg_id = get_new_reg<int>(1);
    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(1);
    int C_reg_id = get_new_reg<int32_t>(C);

    int cond_const_reg_id = get_new_reg<int32_t>(cond_const);
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<int32_t>();
    int a_tile = get_new_tile<int32_t>();
    int cond_tile = get_new_tile<int32_t>();
    int boundaries0_tile = get_new_tile<int>();
    int boundaries1_tile = get_new_tile<int>();
    int i_tile = get_new_tile<int>();
    int j_tile = get_new_tile<int>();
    int cond_res_tile = get_new_tile<uint32_t>();

    int32_t *b_p = get_cacheable_tile_pointer<int32_t>(b_tile);
    int32_t *a_p = get_cacheable_tile_pointer<int32_t>(a_tile);
    uint32_t *cond_res_p = get_cacheable_tile_pointer<uint32_t>(cond_res_tile);

    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tilei_size = i_max - i_base;
        curr_tilei_size = curr_tilei_size > TILE_SIZE ? TILE_SIZE : curr_tilei_size;
        int curr_min = i_base + min;
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(boundaries, min_reg_id, max_reg_id, stride_reg_id, boundaries0_tile);
        maa_stream_load<int>(&boundaries[1], min_reg_id, max_reg_id, stride_reg_id, boundaries1_tile);

        int curr_tilej_size = 0;
        maa_const<int>(0, last_i_reg_id);
        maa_const<int>(-1, last_j_reg_id);
        do {
            maa_range_loop<int32_t>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile);
            maa_indirect_load<int>(idx, j_tile, idx_tile);
            maa_indirect_load<int32_t>(cond, idx_tile, cond_tile);
            maa_alu_scalar<int32_t>(cond_tile, cond_const_reg_id, cond_res_tile, Operation_t::GT_OP);
            maa_indirect_load<int32_t>(b, idx_tile, b_tile, cond_res_tile);
            maa_alu_scalar<int32_t>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP, cond_res_tile);
            maa_indirect_store_vector<int32_t>(a, j_tile, a_tile, cond_res_tile);
            wait_ready(j_tile);
            curr_tilej_size = get_tile_size(j_tile);
        } while (curr_tilej_size > 0);
        wait_ready(a_tile);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_indirectrangeloop_cond(int *nodes, int *boundaries, int32_t *a, int32_t *b, int *idx, int min, int max, const int32_t C, int32_t *cond, const int32_t cond_const) {
    std::cout << "starting gather_rmw_indirectrangeloop_cond min(" << min << "), max(" << max << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
    // TODO: parallelize
    for (int i = min; i < max; i++) {
        for (int j = boundaries[nodes[i]]; j < boundaries[nodes[i] + 1]; j++) {
            if (cond_const < cond[j]) {
                a[idx[j]] += C * b[idx[j]];
            }
        }
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_indirectrangeloop_cond_maa(int *nodes, int *boundaries, int32_t *a, int32_t *b, int *idx, int min, int max, const int32_t C, int32_t *cond, const int32_t cond_const) {
    std::cout << "starting gather_rmw_indirectrangeloop_cond_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    int one_reg_id = get_new_reg<int>(1);
    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(1);
    int C_reg_id = get_new_reg<int32_t>(C);

    int cond_const_reg_id = get_new_reg<int32_t>(cond_const);
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<int32_t>();
    int a_tile = get_new_tile<int32_t>();
    int cond_tile = get_new_tile<int32_t>();
    int boundaries0_tile = get_new_tile<int>();
    int boundaries1_tile = get_new_tile<int>();
    int i_tile = get_new_tile<int>();
    int j_tile = get_new_tile<int>();
    int nodes_tile = get_new_tile<int>();
    int cond_res_tile = get_new_tile<uint32_t>();

    int32_t *b_p = get_cacheable_tile_pointer<int32_t>(b_tile);
    int32_t *a_p = get_cacheable_tile_pointer<int32_t>(a_tile);
    int *i_p = get_cacheable_tile_pointer<int>(i_tile);
    int *j_p = get_cacheable_tile_pointer<int>(j_tile);
    uint32_t *cond_res_p = get_cacheable_tile_pointer<uint32_t>(cond_res_tile);

    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base + min;
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(nodes, min_reg_id, max_reg_id, stride_reg_id, nodes_tile);
        maa_indirect_load<int>(boundaries, nodes_tile, boundaries0_tile);
        maa_indirect_load<int>(&boundaries[1], nodes_tile, boundaries1_tile);

        int curr_tilej_size = 0;
        maa_const<int>(0, last_i_reg_id);
        maa_const<int>(-1, last_j_reg_id);
        do {
            maa_range_loop<int32_t>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile);
            maa_indirect_load<int32_t>(cond, j_tile, cond_tile);
            maa_alu_scalar<int32_t>(cond_tile, cond_const_reg_id, cond_res_tile, Operation_t::GT_OP);
            maa_indirect_load<int>(idx, j_tile, idx_tile, cond_res_tile);
            maa_indirect_load<int32_t>(b, idx_tile, b_tile, cond_res_tile);
            maa_alu_scalar<int32_t>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP, cond_res_tile);
            maa_indirect_rmw_vector<int32_t>(a, idx_tile, a_tile, Operation_t::ADD_OP, cond_res_tile);
            wait_ready(j_tile);
            curr_tilej_size = get_tile_size(j_tile);
        } while (curr_tilej_size > 0);
        wait_ready(a_tile);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_cond_indirectrangeloop_cond(int *nodes, int *boundaries, int32_t *a, int32_t *b, int *idx, int min, int max, const int32_t C, int32_t *cond, const int32_t cond_const) {
    std::cout << "starting gather_rmw_cond_indirectrangeloop_cond min(" << min << "), max(" << max << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
    // TODO: parallelize
    for (int i = min; i < max; i++) {
        if (cond_const < cond[i]) {
            for (int j = boundaries[nodes[i]]; j < boundaries[nodes[i] + 1]; j++) {
                if (cond_const < cond[j]) {
                    a[idx[j]] += C * b[idx[j]];
                }
            }
        }
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_cond_indirectrangeloop_cond_maa(int *nodes, int *boundaries, int32_t *a, int32_t *b, int *idx, int min, int max, const int32_t C, int32_t *cond, const int32_t cond_const) {
    std::cout << "starting gather_rmw_cond_indirectrangeloop_cond_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    int one_reg_id = get_new_reg<int>(1);
    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(1);
    int C_reg_id = get_new_reg<int32_t>(C);

    int cond_const_reg_id = get_new_reg<int32_t>(cond_const);
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<int32_t>();
    int a_tile = get_new_tile<int32_t>();
    int condi_tile = get_new_tile<int32_t>();
    int condj_tile = get_new_tile<int32_t>();
    int boundaries0_tile = get_new_tile<int>();
    int boundaries1_tile = get_new_tile<int>();
    int i_tile = get_new_tile<int>();
    int j_tile = get_new_tile<int>();
    int nodes_tile = get_new_tile<int>();
    int condi_res_tile = get_new_tile<uint32_t>();
    int condj_res_tile = get_new_tile<uint32_t>();

    int32_t *b_p = get_cacheable_tile_pointer<int32_t>(b_tile);
    int32_t *a_p = get_cacheable_tile_pointer<int32_t>(a_tile);
    int *i_p = get_cacheable_tile_pointer<int>(i_tile);
    int *j_p = get_cacheable_tile_pointer<int>(j_tile);
    uint32_t *condj_res_p = get_cacheable_tile_pointer<uint32_t>(condj_res_tile);

    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base + min;
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int32_t>(cond, min_reg_id, max_reg_id, stride_reg_id, condi_tile);
        maa_alu_scalar<int32_t>(condi_tile, cond_const_reg_id, condi_res_tile, Operation_t::GT_OP);
        maa_stream_load<int>(nodes, min_reg_id, max_reg_id, stride_reg_id, nodes_tile, condi_res_tile);
        maa_indirect_load<int>(boundaries, nodes_tile, boundaries0_tile, condi_res_tile);
        maa_indirect_load<int>(&boundaries[1], nodes_tile, boundaries1_tile, condi_res_tile);

        int curr_tilej_size = 0;
        maa_const<int>(0, last_i_reg_id);
        maa_const<int>(-1, last_j_reg_id);
        do {
            maa_range_loop<int32_t>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile, condi_res_tile);
            maa_indirect_load<int32_t>(cond, j_tile, condj_tile);
            maa_alu_scalar<int32_t>(condj_tile, cond_const_reg_id, condj_res_tile, Operation_t::GT_OP);
            maa_indirect_load<int>(idx, j_tile, idx_tile, condj_res_tile);
            maa_indirect_load<int32_t>(b, idx_tile, b_tile, condj_res_tile);
            maa_alu_scalar<int32_t>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP, condj_res_tile);
            maa_indirect_rmw_vector<int32_t>(a, idx_tile, a_tile, Operation_t::ADD_OP, condj_res_tile);
            wait_ready(j_tile);
            curr_tilej_size = get_tile_size(j_tile);
        } while (curr_tilej_size > 0);
        wait_ready(a_tile);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_indirectcond_indirectrangeloop_indirectcond(int *nodes, int *boundaries, int32_t *a, int32_t *b, int *idx, int min, int max, const int32_t C, int32_t *cond, const int32_t cond_const) {
    std::cout << "starting gather_rmw_indirectcond_indirectrangeloop_indirectcond min(" << min << "), max(" << max << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
    // TODO: parallelize
    for (int i = min; i < max; i++) {
        if (cond_const < cond[nodes[i]]) {
            for (int j = boundaries[nodes[i]]; j < boundaries[nodes[i] + 1]; j++) {
                if (cond_const < cond[nodes[j]]) {
                    a[idx[j]] += C * b[idx[j]];
                }
            }
        }
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_indirectcond_indirectrangeloop_indirectcond_maa(int *nodes, int *boundaries, int32_t *a, int32_t *b, int *idx, int min, int max, const int32_t C, int32_t *cond, const int32_t cond_const) {
    std::cout << "starting gather_rmw_indirectcond_indirectrangeloop_indirectcond_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    int one_reg_id = get_new_reg<int>(1);
    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(1);
    int C_reg_id = get_new_reg<int32_t>(C);

    int cond_const_reg_id = get_new_reg<int32_t>(cond_const);
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<int32_t>();
    int a_tile = get_new_tile<int32_t>();
    int condi_tile = get_new_tile<int32_t>();
    int condj_tile = get_new_tile<int32_t>();
    int boundaries0_tile = get_new_tile<int>();
    int boundaries1_tile = get_new_tile<int>();
    int i_tile = get_new_tile<int>();
    int j_tile = get_new_tile<int>();
    int nodesi_tile = get_new_tile<int>();
    int nodesj_tile = get_new_tile<int>();
    int condi_res_tile = get_new_tile<uint32_t>();
    int condj_res_tile = get_new_tile<uint32_t>();

    int32_t *b_p = get_cacheable_tile_pointer<int32_t>(b_tile);
    int32_t *a_p = get_cacheable_tile_pointer<int32_t>(a_tile);
    int *i_p = get_cacheable_tile_pointer<int>(i_tile);
    int *j_p = get_cacheable_tile_pointer<int>(j_tile);
    uint32_t *condj_res_p = get_cacheable_tile_pointer<uint32_t>(condj_res_tile);

    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base + min;
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(nodes, min_reg_id, max_reg_id, stride_reg_id, nodesi_tile);
        maa_indirect_load<int32_t>(cond, nodesi_tile, condi_tile);
        maa_alu_scalar<int32_t>(condi_tile, cond_const_reg_id, condi_res_tile, Operation_t::GT_OP);
        maa_indirect_load<int>(boundaries, nodesi_tile, boundaries0_tile, condi_res_tile);
        maa_indirect_load<int>(&boundaries[1], nodesi_tile, boundaries1_tile, condi_res_tile);

        int curr_tilej_size = 0;
        maa_const<int>(0, last_i_reg_id);
        maa_const<int>(-1, last_j_reg_id);
        do {
            maa_range_loop<int32_t>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile, condi_res_tile);
            maa_indirect_load<int>(nodes, j_tile, nodesj_tile);
            maa_indirect_load<int32_t>(cond, nodesj_tile, condj_tile);
            maa_alu_scalar<int32_t>(condj_tile, cond_const_reg_id, condj_res_tile, Operation_t::GT_OP);
            maa_indirect_load<int>(idx, j_tile, idx_tile, condj_res_tile);
            maa_indirect_load<int32_t>(b, idx_tile, b_tile, condj_res_tile);
            maa_alu_scalar<int32_t>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP, condj_res_tile);
            maa_indirect_rmw_vector<int32_t>(a, idx_tile, a_tile, Operation_t::ADD_OP, condj_res_tile);
            wait_ready(j_tile);
            curr_tilej_size = get_tile_size(j_tile);
        } while (curr_tilej_size > 0);
        wait_ready(a_tile);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void flush_cache() {
    std::cout << "flushing the cache" << std::endl;
    const size_t bigger_than_cachesize = 3024024;
    long *p = new long[bigger_than_cachesize];
// When you want to "flush" cache.
#pragma omp parallel for
    for (int i = 0; i < 1024; i++) {
        std::cout << "iter" << i << std::endl;
        for (int j = 0; j < 3024; j++) {
            p[i * 3024 + j] = rand();
        }
    }
    std::cout << "cache flushed" << std::endl;
}

#define NROWS 65536
#define NBANKS 4
#define NBANKGROUPS 4
#define NCOLS 128
#define NCHANNELS 2
#define NCLBYTES 64
#define NCLWORDS (64 / 4)

inline int index_calculator(int row, int bank, int bankgroup, int cacheline, int channel, int word) {
    int idx;
    idx = row;
    idx *= NBANKS;
    idx += bank;
    idx *= NBANKGROUPS;
    idx += bankgroup;
    idx *= NCOLS;
    idx += cacheline;
    idx *= NCHANNELS;
    idx += channel;
    idx *= NCLWORDS;
    return idx;
}

/*******************************************************************************/
/*******************************************************************************/
/*                                    MAIN                                     */
/*******************************************************************************/
/*******************************************************************************/
const int offset_to_index_8[8] = {0, 1, 3, 6, 2, 5, 7, 4};
uint32_t shuffle_idx_gt8(uint32_t idx) {
    return (idx & 0xfffffff8) + offset_to_index_8[idx & 0x7];
}
const int offset_to_index_4[4] = {0, 3, 1, 2};
uint32_t shuffle_idx_lte4(uint32_t idx) {
    return (idx & 0xfffffff8) + offset_to_index_4[idx & 0x7];
}

// (RO: 9, BA: 0, BG: 0, RA: 0, CO: 64, CH: 0)
// (160 * NCHANNELS * NCLWORDS)
#define IDXA_OFFSET_RMW (96 * NCHANNELS * NCLWORDS)

// MAP(RO: 9, BA: 3, BG: 3, RA: 0, CO: 32, CH: 0)
// (64 * NCHANNELS * NCLWORDS)
#define IDXA_OFFSET_SCATTER (96 * NCHANNELS * NCLWORDS)

// Gather (RO: 73, BA: 0, BG: 0, RA: 0, CO: 32, CH: 0)
// (128 * NCHANNELS * NCLWORDS)
#define IDXB_OFFSET_GATHER (96 * NCHANNELS * NCLWORDS)

// Gather (RO: 8, BA: 0, BG: 0, RA: 0, CO: 96, CH: 0)
// (128 * NCHANNELS * NCLWORDS)
// (RO: 8, BA: 0, BG: 0, RA: 0, CO: 64, CH: 0)
// (32 * NCHANNELS * NCLWORDS)
#define IDXB_OFFSET_GATHER_INDIR (NBANKS * NBANKGROUPS * NCHANNELS * NCLWORDS - 32 * NCHANNELS * NCLWORDS)

// Range (RO: 780, BA: 0, BG: 3, RA: 0, CO: 32, CH: 0)
// (128 * NCHANNELS * NCLWORDS)
#define IDXC_OFFSET (129 * NCHANNELS * NCLWORDS + 32 * NCHANNELS)

void allmiss_allocator(std::string kernel, int32_t *&a1, int32_t *&a2, int32_t *&b, int *&idx, int *&boundaries, int32_t *&cond, int &n, bool is_range, bool is_cond, int &num_required_elements, int &num_rows_per_tile, bool maa, bool base) {
    const int IDXA_OFFSET = kernel == "rmw" ? IDXA_OFFSET_RMW : kernel == "scatter" ? IDXA_OFFSET_SCATTER
                                                                                    : 0;
    const int IDXB_OFFSET = kernel == "gather" || kernel == "gather_full" ? IDXB_OFFSET_GATHER : kernel == "gather_directrangeloop_indircond" ? IDXB_OFFSET_GATHER_INDIR
                                                                                                                                              : 0;
    if (is_range) {
        n *= 4;
    }
    const unsigned int RO_size_bytes = NBANKGROUPS * NBANKS * NCOLS * NCLBYTES * NCHANNELS;
    num_rows_per_tile = TILE_SIZE / NCOLS / NBANKGROUPS / NCHANNELS;
    // Because we skip banks and cacheline words
    num_required_elements = (((n - 1) / TILE_SIZE) + 1) * TILE_SIZE * NBANKS * NCLWORDS;
    if (base) {
        a1 = (int32_t *)aligned_alloc(RO_size_bytes, sizeof(int32_t) * (num_required_elements + IDXA_OFFSET));
    }
    if (maa) {
        a2 = (int32_t *)aligned_alloc(RO_size_bytes, sizeof(int32_t) * (num_required_elements + IDXA_OFFSET));
    }
    b = (int32_t *)aligned_alloc(RO_size_bytes, sizeof(int32_t) * (num_required_elements + IDXB_OFFSET));
    idx = (int *)aligned_alloc(RO_size_bytes, sizeof(int) * num_required_elements);
    if (is_range) {
        boundaries = (int *)aligned_alloc(RO_size_bytes, sizeof(int) * (num_required_elements + 1));
    }
    if (is_cond) {
        cond = (int32_t *)aligned_alloc(RO_size_bytes, sizeof(int32_t) * (num_required_elements + IDXC_OFFSET));
    }
}
void allmiss_initializer(std::string kernel, int32_t *&a1, int32_t *&a2, int32_t *&b, int *&idx, int *&boundaries, int32_t *&cond, bool is_range, bool is_cond, int num_required_elements, bool maa, bool base) {
    const int IDXA_OFFSET = kernel == "rmw" ? IDXA_OFFSET_RMW : kernel == "scatter" ? IDXA_OFFSET_SCATTER
                                                                                    : 0;
    const int IDXB_OFFSET = kernel == "gather" || kernel == "gather_full" ? IDXB_OFFSET_GATHER : kernel == "gather_directrangeloop_indircond" ? IDXB_OFFSET_GATHER_INDIR
                                                                                                                                              : 0;
    if (base) {
        std::cout << "initializing a1" << std::endl;
        for (int i = 0; i < num_required_elements + IDXA_OFFSET; i++) {
            a1[i] = i;
        }
        a1 = &a1[IDXA_OFFSET];
    }
    if (maa) {
        std::cout << "initializing a2" << std::endl;
        for (int i = 0; i < num_required_elements + IDXA_OFFSET; i++) {
            a2[i] = i;
        }
        a2 = &a2[IDXA_OFFSET];
    }
    std::cout << "initializing b" << std::endl;
    for (int i = 0; i < num_required_elements + IDXB_OFFSET; i++) {
        b[i] = i;
    }
    b = &b[IDXB_OFFSET];
    if (is_range) {
        std::cout << "initializing boundaries" << std::endl;
        for (int i = 0; i < num_required_elements + 1; i++) {
            boundaries[i] = 4 * i;
        }
    }
    if (is_cond) {
        std::cout << "initializing cond" << std::endl;
        for (int i = 0; i < num_required_elements + IDXC_OFFSET; i++) {
            cond[i] = (i & 1) * 256;
        }
        cond = &cond[IDXC_OFFSET];
    }
}
void allmiss_BAmiss_RBhitmiss_CHmiss_BGmiss_initializer(std::string kernel, int32_t *&a1, int32_t *&a2, int32_t *&b, int *&idx, int *&boundaries, int32_t *&cond, int n, int RBH, bool is_range, bool is_cond, int &num_required_elements, bool maa, bool base) {
    int num_rows_per_tile;
    allmiss_allocator(kernel, a1, a2, b, idx, boundaries, cond, n, is_range, is_cond, num_required_elements, num_rows_per_tile, maa, base);
    const unsigned int NCOLH = max(1, RBH * NCOLS / 100);
    std::cout << "initializing general arrays: " << num_required_elements << " elements" << std::endl;
    int i = 0;
    int row_offset = 0;
    for (uint32_t tile_element = 0; tile_element < n && i < n; tile_element += TILE_SIZE, row_offset += num_rows_per_tile) {
        std::cout << "initializing TE " << tile_element << std::endl;
        for (uint32_t CH = 0; CH < NCHANNELS && i < n; CH++) {
            for (uint32_t BG = 0; BG < NBANKGROUPS && i < n; BG++) {
                for (uint32_t CLM = 0; CLM < NCOLS && i < n; CLM += NCOLH) {
                    for (uint32_t row = 0; row < num_rows_per_tile && i < n; row++) {
                        for (uint32_t CLH = 0; CLH < NCOLH && i < n; CLH++, i++) {
                            uint32_t RO = row_offset + shuffle_idx_gt8(row);
                            uint32_t CL = CLM + CLH;
                            uint32_t new_CL = shuffle_idx_gt8(CL);
                            uint32_t new_BG = shuffle_idx_lte4(BG);
                            // index_calculator(row,bank,bankgroup,cacheline,channel,word)
                            uint32_t index = index_calculator(RO, 0, new_BG, new_CL, CH, 0);
                            idx[i] = index;
#ifndef GEM5
                            if (index >= num_required_elements) {
                                std::cout << "i: " << i << ", TE: " << tile_element << ", CH: " << CH << ", BG: " << new_BG << ", RO: " << RO << ", CL: " << new_CL << ", index: " << index << " >= " << num_required_elements << std::endl;
                                exit(-1);
                            }
#endif
                        }
                    }
                }
            }
        }
    }
    allmiss_initializer(kernel, a1, a2, b, idx, boundaries, cond, is_range, is_cond, num_required_elements, maa, base);
}
void allmiss_BAhit_RBhitmiss_CHmiss_BGmiss_initializer(std::string kernel, int32_t *&a1, int32_t *&a2, int32_t *&b, int *&idx, int *&boundaries, int32_t *&cond, int n, int RBH, bool is_range, bool is_cond, int &num_required_elements, bool maa, bool base) {
    int num_rows_per_tile;
    allmiss_allocator(kernel, a1, a2, b, idx, boundaries, cond, n, is_range, is_cond, num_required_elements, num_rows_per_tile, maa, base);
    const unsigned int NCOLH = max(1, RBH * NCOLS / 100);
    std::cout << "initializing general arrays: " << num_required_elements << " elements" << std::endl;
    int i = 0;
    int BA = 0;
    for (uint32_t tile_element = 0; tile_element < n && i < n; tile_element += TILE_SIZE) {
        std::cout << "initializing TE " << tile_element << std::endl;
        for (uint32_t CH = 0; CH < NCHANNELS && i < n; CH++) {
            for (uint32_t BG = 0; BG < NBANKGROUPS && i < n; BG++) {
                for (uint32_t CLM = 0; CLM < NCOLS && i < n; CLM += NCOLH) {
                    for (uint32_t row = 0; row < num_rows_per_tile && i < n; row++) {
                        for (uint32_t CLH = 0; CLH < NCOLH && i < n; CLH++, i++) {
                            uint32_t RO = shuffle_idx_gt8(row);
                            uint32_t CL = CLM + CLH;
                            uint32_t new_CL = shuffle_idx_gt8(CL);
                            uint32_t new_BG = shuffle_idx_lte4(BG);
                            // index_calculator(row,bank,bankgroup,cacheline,channel,word)
                            uint32_t index = index_calculator(RO, BA, new_BG, new_CL, CH, 0);
                            idx[i] = index;
#ifndef GEM5
                            if (index >= num_required_elements) {
                                std::cout << "i: " << i << ", TE: " << tile_element << ", CH: " << CH << ", BG: " << new_BG << ", RO: " << RO << ", CL: " << new_CL << ", index: " << index << " >= " << num_required_elements << std::endl;
                                exit(-1);
                            }
#endif
                        }
                    }
                }
            }
        }
        if (BA == NBANKS - 1) {
            BA = 0;
        } else {
            BA++;
        }
    }
    allmiss_initializer(kernel, a1, a2, b, idx, boundaries, cond, is_range, is_cond, num_required_elements, maa, base);
}
void allmiss_BAhit_RBhit_CHhit_BGmiss_initializer(std::string kernel, int32_t *&a1, int32_t *&a2, int32_t *&b, int *&idx, int *&boundaries, int32_t *&cond, int n, bool is_range, bool is_cond, int &num_required_elements, bool maa, bool base) {
    int num_rows_per_tile;
    allmiss_allocator(kernel, a1, a2, b, idx, boundaries, cond, n, is_range, is_cond, num_required_elements, num_rows_per_tile, maa, base);
    std::cout << "initializing general arrays: " << num_required_elements << " elements" << std::endl;
    int i = 0;
    int BA = 0;
    for (uint32_t tile_element = 0; tile_element < n && i < n; tile_element += TILE_SIZE) {
        std::cout << "initializing TE " << tile_element << std::endl;
        for (uint32_t BG = 0; BG < NBANKGROUPS && i < n; BG++) {
            for (uint32_t row = 0; row < num_rows_per_tile && i < n; row++) {
                for (uint32_t CL = 0; CL < NCOLS && i < n; CL++) {
                    for (uint32_t CH = 0; CH < NCHANNELS && i < n; CH++, i++) {
                        uint32_t RO = shuffle_idx_gt8(row);
                        uint32_t new_CL = shuffle_idx_gt8(CL);
                        uint32_t new_BG = shuffle_idx_lte4(BG);
                        // index_calculator(row,bank,bankgroup,cacheline,channel,word)
                        uint32_t index = index_calculator(RO, BA, new_BG, new_CL, CH, 0);
                        idx[i] = index;
#ifndef GEM5
                        if (index >= num_required_elements) {
                            std::cout << "i: " << i << ", TE: " << tile_element << ", CH: " << CH << ", BG: " << new_BG << ", RO: " << RO << ", CL: " << new_CL << ", index: " << index << " >= " << num_required_elements << std::endl;
                            exit(-1);
                        }
#endif
                    }
                }
            }
        }
        if (BA == NBANKS - 1) {
            BA = 0;
        } else {
            BA++;
        }
    }
    allmiss_initializer(kernel, a1, a2, b, idx, boundaries, cond, is_range, is_cond, num_required_elements, maa, base);
}
void allmiss_BAhit_RBhit_CHhit_BGhit_initializer(std::string kernel, int32_t *&a1, int32_t *&a2, int32_t *&b, int *&idx, int *&boundaries, int32_t *&cond, int n, bool is_range, bool is_cond, int &num_required_elements, bool maa, bool base) {
    int num_rows_per_tile;
    allmiss_allocator(kernel, a1, a2, b, idx, boundaries, cond, n, is_range, is_cond, num_required_elements, num_rows_per_tile, maa, base);
    std::cout << "initializing general arrays: " << num_required_elements << " elements" << std::endl;
    int i = 0;
    int BA = 0;
    for (uint32_t tile_element = 0; tile_element < n && i < n; tile_element += TILE_SIZE) {
        std::cout << "initializing TE " << tile_element << std::endl;
        for (uint32_t row = 0; row < num_rows_per_tile && i < n; row++) {
            for (uint32_t CL = 0; CL < NCOLS && i < n; CL++) {
                for (uint32_t BG = 0; BG < NBANKGROUPS && i < n; BG++) {
                    for (uint32_t CH = 0; CH < NCHANNELS && i < n; CH++, i++) {
                        uint32_t RO = shuffle_idx_gt8(row);
                        uint32_t new_CL = shuffle_idx_gt8(CL);
                        uint32_t new_BG = shuffle_idx_lte4(BG);
                        // index_calculator(row,bank,bankgroup,cacheline,channel,word)
                        uint32_t index = index_calculator(RO, BA, new_BG, new_CL, CH, 0);
                        idx[i] = index;
#ifndef GEM5
                        if (index >= num_required_elements) {
                            std::cout << "i: " << i << ", TE: " << tile_element << ", CH: " << CH << ", BG: " << new_BG << ", RO: " << RO << ", CL: " << new_CL << ", index: " << index << " >= " << num_required_elements << std::endl;
                            exit(-1);
                        }
#endif
                    }
                }
            }
        }
        if (BA == NBANKS - 1) {
            BA = 0;
        } else {
            BA++;
        }
    }
    allmiss_initializer(kernel, a1, a2, b, idx, boundaries, cond, is_range, is_cond, num_required_elements, maa, base);
}
void allhit_initializer(std::string kernel, int32_t *&a1, int32_t *&a2, int32_t *&b, int *&idx, int *&boundaries, int32_t *&cond, int n, bool is_range, bool is_cond, int &num_required_elements) {
    if (is_range) {
        n *= 4;
    }
    const unsigned int RO_size_bytes = NBANKGROUPS * NBANKS * NCOLS * NCLBYTES * NCHANNELS;
    num_required_elements = n;
    a1 = (int32_t *)aligned_alloc(RO_size_bytes, sizeof(int32_t) * num_required_elements);
    a2 = (int32_t *)aligned_alloc(RO_size_bytes, sizeof(int32_t) * num_required_elements);
    b = (int32_t *)aligned_alloc(RO_size_bytes, sizeof(int32_t) * num_required_elements);
    idx = (int *)aligned_alloc(RO_size_bytes, sizeof(int) * num_required_elements);
    if (is_range) {
        boundaries = (int *)aligned_alloc(RO_size_bytes, sizeof(int) * (num_required_elements + 1));
    }
    if (is_cond) {
        cond = (int32_t *)aligned_alloc(RO_size_bytes, sizeof(int32_t) * num_required_elements);
    }
    std::cout << "initializing general arrays: " << num_required_elements << " elements" << std::endl;
    for (uint32_t i = 0; i < num_required_elements; i++) {
        idx[i] = i;
    }
    std::cout << "initializing a1" << std::endl;
    for (int i = 0; i < num_required_elements; i++) {
        a1[i] = i;
    }
    std::cout << "initializing a2" << std::endl;
    for (int i = 0; i < num_required_elements; i++) {
        a2[i] = i;
    }
    std::cout << "initializing b" << std::endl;
    for (int i = 0; i < num_required_elements; i++) {
        b[i] = i;
    }
    if (is_range) {
        std::cout << "initializing boundaries" << std::endl;
        for (int i = 0; i < num_required_elements + 1; i++) {
            boundaries[i] = 4 * i;
        }
    }
    if (is_cond) {
        std::cout << "initializing cond" << std::endl;
        for (int i = 0; i < num_required_elements; i++) {
            cond[i] = (i & 1) * 256;
        }
    }
}
void allhitl3_initializer(std::string kernel, int32_t *&a1, int32_t *&a2, int32_t *&b, int *&idx, int *&boundaries, int32_t *&cond, int n, bool is_range, bool is_cond, int &num_required_elements) {
    if (is_range) {
        n *= 4;
    }
    const unsigned int RO_size_bytes = NBANKGROUPS * NBANKS * NCOLS * NCLBYTES * NCHANNELS;
    num_required_elements = n * NCLWORDS;
    a1 = (int32_t *)aligned_alloc(RO_size_bytes, sizeof(int32_t) * num_required_elements);
    a2 = (int32_t *)aligned_alloc(RO_size_bytes, sizeof(int32_t) * num_required_elements);
    b = (int32_t *)aligned_alloc(RO_size_bytes, sizeof(int32_t) * num_required_elements);
    idx = (int *)aligned_alloc(RO_size_bytes, sizeof(int) * num_required_elements);
    if (is_range) {
        boundaries = (int *)aligned_alloc(RO_size_bytes, sizeof(int) * (num_required_elements + 1));
    }
    if (is_cond) {
        cond = (int32_t *)aligned_alloc(RO_size_bytes, sizeof(int32_t) * num_required_elements);
    }
    std::cout << "initializing general arrays: " << num_required_elements << " elements" << std::endl;
    for (uint32_t i = 0; i < num_required_elements; i++) {
        idx[i] = shuffle_idx_gt8(i) * NCLWORDS;
    }
    std::cout << "initializing a1" << std::endl;
    for (int i = 0; i < num_required_elements; i++) {
        a1[i] = i;
    }
    std::cout << "initializing a2" << std::endl;
    for (int i = 0; i < num_required_elements; i++) {
        a2[i] = i;
    }
    std::cout << "initializing b" << std::endl;
    for (int i = 0; i < num_required_elements; i++) {
        b[i] = i;
    }
    if (is_range) {
        std::cout << "initializing boundaries" << std::endl;
        for (int i = 0; i < num_required_elements + 1; i++) {
            boundaries[i] = 4 * i;
        }
    }
    if (is_cond) {
        std::cout << "initializing cond" << std::endl;
        for (int i = 0; i < num_required_elements; i++) {
            cond[i] = (i & 1) * 256;
        }
    }
}
void random_initializer(std::string kernel, int32_t *&a1, int32_t *&a2, int32_t *&b, int *&idx, int *&boundaries, int32_t *&cond, int n, bool is_range, bool is_cond, int &num_required_elements, int distance) {
    if (is_range) {
        n *= 4;
    }
    const unsigned int RO_size_bytes = NBANKGROUPS * NBANKS * NCOLS * NCLBYTES * NCHANNELS;
    num_required_elements = n;
    a1 = (int32_t *)aligned_alloc(RO_size_bytes, sizeof(int32_t) * num_required_elements);
    a2 = (int32_t *)aligned_alloc(RO_size_bytes, sizeof(int32_t) * num_required_elements);
    b = (int32_t *)aligned_alloc(RO_size_bytes, sizeof(int32_t) * num_required_elements);
    idx = (int *)aligned_alloc(RO_size_bytes, sizeof(int) * num_required_elements);
    if (is_range) {
        boundaries = (int *)aligned_alloc(RO_size_bytes, sizeof(int) * (num_required_elements + 1));
    }
    if (is_cond) {
        cond = (int32_t *)aligned_alloc(RO_size_bytes, sizeof(int32_t) * num_required_elements);
    }
    std::cout << "initializing random general arrays: " << num_required_elements << " elements" << std::endl;
    for (int i = 0; i < num_required_elements; i++) {
        int tmp = rand() % (2 * distance) % num_required_elements;
        idx[i] = (i - tmp) < 0 ? tmp : (i - tmp);
    }
    std::cout << "initializing a1" << std::endl;
    for (int i = 0; i < num_required_elements; i++) {
        a1[i] = i;
    }
    std::cout << "initializing a2" << std::endl;
    for (int i = 0; i < num_required_elements; i++) {
        a2[i] = i;
    }
    std::cout << "initializing b" << std::endl;
    for (int i = 0; i < num_required_elements; i++) {
        b[i] = i;
    }
    if (is_range) {
        std::cout << "initializing boundaries" << std::endl;
        for (int i = 0; i < num_required_elements + 1; i++) {
            boundaries[i] = 4 * i;
        }
    }
    if (is_cond) {
        std::cout << "initializing cond" << std::endl;
        for (int i = 0; i < num_required_elements; i++) {
            cond[i] = (i & 1) * 256;
        }
    }
}
void l3_warmer_int(int32_t *a, int size) {
    init_MAA();
    std::cout << "initalizing an integer array of size " << size << std::endl;
    int max_reg_id, min_reg_id, stride_reg_id, a_tile;
    max_reg_id = get_new_reg<int>(size);
    min_reg_id = get_new_reg<int>();
    stride_reg_id = get_new_reg<int>(1);
    a_tile = get_new_tile<int32_t>();
    for (int i = 0; i < size; i += TILE_SIZE) {
        int curr_tile_size = size - i;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        maa_const(i, min_reg_id);
        maa_stream_load<int32_t>(a, min_reg_id, max_reg_id, stride_reg_id, a_tile);
    }
    wait_ready(a_tile);
}
void l3_warmer_float(float *a, int size) {
    init_MAA();
    std::cout << "initalizing a floating-point array of size " << size << std::endl;
    int max_reg_id, min_reg_id, stride_reg_id, a_tile;
    max_reg_id = get_new_reg<int>(size);
    min_reg_id = get_new_reg<int>();
    stride_reg_id = get_new_reg<int>(1);
    a_tile = get_new_tile<float>();
    for (int i = 0; i < size; i += TILE_SIZE) {
        int curr_tile_size = size - i;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        maa_const(i, min_reg_id);
        maa_stream_load<float>(a, min_reg_id, max_reg_id, stride_reg_id, a_tile);
    }
    wait_ready(a_tile);
}
void print_usage(std::string name) {
    cout << "Usage: " << name << " [n] [BASE|MAA|CMP] [kernel|all] [allhit|allhitl3|allmiss|random] [dist_args]" << endl;
    cout << "\t\tallhit:\tno args" << endl;
    cout << "\t\tallhitl3:\tno args" << endl;
    cout << "\t\tallmiss:\t[BAH] [ROH] [CHH] [BGH]" << endl;
    cout << "\t\trandom:\t[distance]" << endl;
}
int main(int argc, char *argv[]) {

    if (argc < 5) {
        print_usage(argv[0]);
        return 1;
    }

    srand((unsigned)time(NULL));
    int n = stoi(argv[1]);
    string mode = argv[2];
    string kernel = argv[3];
    string dist = argv[4];

    if (dist == "allhit") {
        if (argc != 5) {
            cout << "Usage: " << argv[0] << " [n] [BASE|MAA|CMP] [kernel|all] allhit" << endl;
            return 1;
        }
    } else if (dist == "allhitl3") {
        if (argc != 5) {
            cout << "Usage: " << argv[0] << " [n] [BASE|MAA|CMP] [kernel|all] allhitl3" << endl;
            return 1;
        }
    } else if (dist == "allmiss") {
        if (argc != 9) {
            cout << "Usage: " << argv[0] << " [n] [BASE|MAA|CMP] [kernel|all] allmiss [ROH] [CHH] [BGH]" << endl;
            return 1;
        }
    } else if (dist == "random") {
        if (argc != 6) {
            cout << "Usage: " << argv[0] << " [n] [BASE|MAA|CMP] [kernel|all] random [distance]" << endl;
            return 1;
        }
    } else {
        cout << "Error: Unknown distribution " << dist << endl;
        print_usage(argv[0]);
        return 1;
    }

    bool base = false;
    bool maa = false;
    bool cmp = false;
    if (mode == "BASE") {
        base = true;
    } else if (mode == "MAA") {
        maa = true;
    } else if (mode == "CMP") {
        base = true;
        maa = true;
        cmp = true;
    } else {
        cout << "Error: Unknown mode " << mode << endl;
        print_usage(argv[0]);
        return 1;
    }

    int *nodes;
    int32_t *a1;
    int32_t *a2;
    int32_t *c1;
    int32_t *c2;
    int32_t *b;
    int *idx;
    int *boundaries;
    int32_t *cond;
    bool is_range = kernel.find("range") != std::string::npos;
    bool is_cond = kernel.find("cond") != std::string::npos;

    int num_required_elements;
    c1 = (int32_t *)malloc(sizeof(int32_t) * n);
    c2 = (int32_t *)malloc(sizeof(int32_t) * n);
    for (int i = 0; i < n; i++) {
        c1[i] = -1;
        c2[i] = -1;
    }
    if (dist == "allhit") {
        allhit_initializer(kernel, a1, a2, b, idx, boundaries, cond, n, is_range, is_cond, num_required_elements);
    } else if (dist == "allhitl3") {
        allhitl3_initializer(kernel, a1, a2, b, idx, boundaries, cond, n, is_range, is_cond, num_required_elements);
    } else if (dist == "allmiss") {
        int BAH = stoi(argv[5]);
        int ROH = stoi(argv[6]);
        int CHH = stoi(argv[7]);
        int BGH = stoi(argv[8]);
        if (BAH == 1 && ROH == 100 && CHH == 1 && BGH == 1) {
            allmiss_BAhit_RBhit_CHhit_BGhit_initializer(kernel, a1, a2, b, idx, boundaries, cond, n, is_range, is_cond, num_required_elements, maa, base);
        } else if (BAH == 1 && ROH == 100 && CHH == 1 && BGH == 0) {
            allmiss_BAhit_RBhit_CHhit_BGmiss_initializer(kernel, a1, a2, b, idx, boundaries, cond, n, is_range, is_cond, num_required_elements, maa, base);
        } else if (BAH == 1 && ROH >= 0 && ROH <= 100 && CHH == 0 && BGH == 0) {
            allmiss_BAhit_RBhitmiss_CHmiss_BGmiss_initializer(kernel, a1, a2, b, idx, boundaries, cond, n, ROH, is_range, is_cond, num_required_elements, maa, base);
        } else if (BAH == 0 && ROH >= 0 && ROH <= 100 && CHH == 0 && BGH == 0) {
            allmiss_BAmiss_RBhitmiss_CHmiss_BGmiss_initializer(kernel, a1, a2, b, idx, boundaries, cond, n, ROH, is_range, is_cond, num_required_elements, maa, base);
        } else {
            cout << "Usage: " << argv[0] << " [n] [BASE|MAA|CMP] [kernel|all] allmiss [BAH] [ROH] [CHH] [BGH]" << endl;
            cout << "Available options for\tBAH\tROH\tCHH\tBGH:" << std::endl;
            cout << "\t\t\t0\t[0-100]\t0\t0" << std::endl;
            cout << "\t\t\t1\t[0-100]\t0\t0" << std::endl;
            cout << "\t\t\t1\t100\t1\t0" << std::endl;
            cout << "\t\t\t1\t100\t1\t1" << std::endl;
            return 1;
        }
    } else if (dist == "random") {
        int distance = stoi(argv[5]);
        random_initializer(kernel, a1, a2, b, idx, boundaries, cond, n, is_range, is_cond, num_required_elements, distance);
    } else {
        cout << "Error: Unknown distribution " << dist << endl;
        print_usage(argv[0]);
        return 1;
    }

    int min = 0;
    int max = n;

#ifdef GEM5
    std::cout << "Checkpointing started" << std::endl;
    m5_checkpoint(0, 0);
    std::cout << "Checkpointing ended" << std::endl;
#endif

    std::cout << "initializing done, testing..." << std::endl;

    if (maa || dist == "allhitl3") {
        alloc_MAA();
    }

#ifdef GEM5
    m5_add_mem_region((void *)a1, (void *)(&a1[num_required_elements]), 6);
    m5_add_mem_region((void *)a2, (void *)(&a2[num_required_elements]), 7);
    m5_add_mem_region((void *)b, (void *)(&b[num_required_elements]), 8);
    m5_add_mem_region((void *)idx, (void *)(&idx[num_required_elements]), 9);
    m5_add_mem_region((void *)boundaries, (void *)(&boundaries[num_required_elements + 1]), 10);
    m5_add_mem_region((void *)cond, (void *)(&cond[num_required_elements]), 11);
#endif

    if (kernel == "gather" || kernel == "all") {
        if (maa) {
            if (dist == "allhitl3") {
                l3_warmer_int(a2, num_required_elements);
                l3_warmer_int(b, num_required_elements);
                l3_warmer_int(idx, num_required_elements);
            } else if (dist != "allmiss") {
                // for warming up a2 and idx in correct cores
                gather_maa(a2, b, idx, min, max, 3);
                // for warming up b in LLC
                l3_warmer_int(b, num_required_elements);
            }
            gather_maa(a2, b, idx, min, max, 3);
        }
        if (base) {
            if (dist == "allhitl3") {
                l3_warmer_int(a1, num_required_elements);
                l3_warmer_int(b, num_required_elements);
                l3_warmer_int(idx, num_required_elements);
            } else if (dist != "allmiss") {
                gather(a1, b, idx, min, max, 3);
            }
            gather(a1, b, idx, min, max, 3);
        }
        if (cmp) {
            for (int i = 0; i < n; i++) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- Gather: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(a1);
                    free(a2);
                    free(b);
                    free(idx);
                    return 1;
                }
            }
            cout << "gather correct" << std::endl;
        }
    }

    if (kernel == "gather_full" || kernel == "all") {
        if (maa) {
            if (dist == "allhitl3") {
                l3_warmer_int(a2, num_required_elements);
                l3_warmer_int(b, num_required_elements);
                l3_warmer_int(idx, num_required_elements);
            } else if (dist != "allmiss") {
                // for warming up a2 and idx in correct cores
                gather_full_maa(a2, b, idx, min, max, 3);
                // for warming up b in LLC
                l3_warmer_int(b, num_required_elements);
            }
            gather_full_maa(a2, b, idx, min, max, 3);
        }
        if (base) {
            if (dist == "allhitl3") {
                l3_warmer_int(a1, num_required_elements);
                l3_warmer_int(b, num_required_elements);
                l3_warmer_int(idx, num_required_elements);
            } else if (dist != "allmiss") {
                gather(a1, b, idx, min, max, 3);
            }
            gather(a1, b, idx, min, max, 3);
        }
        if (cmp) {
            for (int i = 0; i < n; i++) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- gather_full: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(a1);
                    free(a2);
                    free(b);
                    free(idx);
                    return 1;
                }
            }
            cout << "gather_full correct" << std::endl;
        }
    }

    if (kernel == "scatter" || kernel == "all") {
        if (maa) {
            if (dist == "allhitl3") {
                l3_warmer_int(a2, num_required_elements);
                l3_warmer_int(b, num_required_elements);
                l3_warmer_int(idx, num_required_elements);
            } else if (dist != "allmiss") {
                // for warming up b and idx in correct cores
                scatter_maa(a2, b, idx, min, max, 3);
                // for warming up a2 in LLC
                l3_warmer_int(a2, num_required_elements);
            }
            scatter_maa(a2, b, idx, min, max, 3);
        }
        if (base) {
            if (dist == "allhitl3") {
                l3_warmer_int(a1, num_required_elements);
                l3_warmer_int(b, num_required_elements);
                l3_warmer_int(idx, num_required_elements);
            } else if (dist != "allmiss") {
                scatter(a1, b, idx, min, max, 3);
            }
            scatter(a1, b, idx, min, max, 3);
        }
        if (cmp) {
            for (int i = 0; i < n; i++) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- scatter: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(a1);
                    free(a2);
                    free(b);
                    free(idx);
                    return 1;
                }
            }
            cout << "scatter correct" << std::endl;
        }
    }

    if (kernel == "rmw" || kernel == "all") {
        if (maa) {
            if (dist == "allhitl3") {
                l3_warmer_int(a2, num_required_elements);
                l3_warmer_int(b, num_required_elements);
                l3_warmer_int(idx, num_required_elements);
            } else if (dist != "allmiss") {
                // for warming up b and idx in correct cores
                rmw_maa(a2, b, idx, min, max, 3);
                // for warming up a2 in LLC
                l3_warmer_int(a2, num_required_elements);
            }
            rmw_maa(a2, b, idx, min, max, 3);
        }
        if (base) {
            if (dist == "allhitl3") {
                l3_warmer_int(a1, num_required_elements);
                l3_warmer_int(b, num_required_elements);
                l3_warmer_int(idx, num_required_elements);
            } else if (dist != "allmiss") {
                rmw(a1, b, idx, min, max, 3);
            }
            rmw(a1, b, idx, min, max, 3);
        }
        if (cmp) {
            for (int i = 0; i < n; i++) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- rmw: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(a1);
                    free(a2);
                    free(b);
                    free(idx);
                    return 1;
                }
            }
            cout << "rmw correct" << std::endl;
        }
    }
    if (kernel == "gather_rmw_dst" || kernel == "all") {
        if (maa) {
            if (dist == "allhitl3") {
                l3_warmer_int(c2, num_required_elements);
                l3_warmer_int(a2, num_required_elements);
                l3_warmer_int(b, num_required_elements);
                l3_warmer_int(idx, num_required_elements);
            } else if (dist != "allmiss") {
                // for warming up b and idx in correct cores
                gather_rmw_dst_maa(a2, b, c2, idx, min, max, 3);
                // for warming up a2 in LLC
                l3_warmer_int(a2, num_required_elements);
            }
            gather_rmw_dst_maa(a2, b, c2, idx, min, max, 3);
        }
        if (base) {
            if (dist == "allhitl3") {
                l3_warmer_int(c1, num_required_elements);
                l3_warmer_int(a1, num_required_elements);
                l3_warmer_int(b, num_required_elements);
                l3_warmer_int(idx, num_required_elements);
            } else if (dist != "allmiss") {
                gather_rmw_dst(a1, b, c1, idx, min, max, 3);
            }
            gather_rmw_dst(a1, b, c1, idx, min, max, 3);
        }
        if (cmp) {
            for (int i = 0; i < n; i++) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- rmw: a[" << i << "] " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(a1);
                    free(a2);
                    free(c1);
                    free(c2);
                    free(b);
                    free(idx);
                    return 1;
                }
                if (abs(c1[i] - c2[i]) >= DELTA) {
                    cout << "Error -- rmw: c[" << i << "] " << c1[i] << " " << c2[i] << endl;
                    free(nodes);
                    free(a1);
                    free(a2);
                    free(c1);
                    free(c2);
                    free(b);
                    free(idx);
                    return 1;
                }
            }
            cout << "gather_rmw_dst correct" << std::endl;
        }
    }

    //     if (kernel == "gather_scatter" || kernel == "all") {
    //         min = 0;
    //         max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    //         if (maa) {
    //             gather_scatter_maa(a2, b, idx, min, max, C);
    //         }
    //         if (base) {
    //             gather_scatter(a1, b, idx, min, max, C);
    //         }
    //         if (cmp) {
    //             for (int i = 0; i < (long)n * S; i++) {
    //                 if (abs(a1[i] - a2[i]) >= DELTA) {
    //                     cout << "Error -- gather_scatter: " << i << " " << a1[i] << " " << a2[i] << endl;
    //                     free(nodes);
    //                     free(a1);
    //                     free(a2);
    //                     free(b);
    //                     free(idx);
    //                     return 1;
    //                 }
    //             }
    //             cout << "gather_scatter correct" << std::endl;
    //         }
    //     }

    //     if (kernel == "gather_rmw" || kernel == "all") {
    //         min = 0;
    //         max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    //         if (maa) {
    //             gather_rmw_maa(a2, b, idx, min, max, C);
    //         }
    //         if (base) {
    //             gather_rmw(a1, b, idx, min, max, C);
    //         }
    //         if (cmp) {
    //             for (int i = 0; i < (long)n * S; i++) {
    //                 if (abs(a1[i] - a2[i]) >= DELTA) {
    //                     cout << "Error -- gather_rmw: " << i << " " << a1[i] << " " << a2[i] << endl;
    //                     free(nodes);
    //                     free(a1);
    //                     free(a2);
    //                     free(b);
    //                     free(idx);
    //                     return 1;
    //                 }
    //             }
    //             cout << "gather_rmw correct" << std::endl;
    //         }
    //     }

    //     if (kernel == "gather_rmw_cond" || kernel == "all") {
    //         min = 0;
    //         max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    //         if (maa) {
    //             gather_rmw_cond_maa(a2, b, idx, min, max, C, cond, 128);
    //         }
    //         if (base) {
    //             gather_rmw_cond(a1, b, idx, min, max, C, cond, 128);
    //         }
    //         if (cmp) {
    //             for (int i = 0; i < (long)n * S; i++) {
    //                 if (abs(a1[i] - a2[i]) >= DELTA) {
    //                     cout << "Error -- gather_rmw_cond: " << i << " " << a1[i] << " " << a2[i] << endl;
    //                     free(nodes);
    //                     free(a1);
    //                     free(a2);
    //                     free(b);
    //                     free(idx);
    //                     return 1;
    //                 }
    //             }
    //             cout << "gather_rmw_cond correct" << std::endl;
    //         }
    //     }
    // if (kernel == "gather_rmw_directrangeloop_cond" || kernel == "all") {
    //     if (maa) {
    //         if (dist == "allhitl3") {
    //             l3_warmer_int(a2, num_required_elements);
    //             l3_warmer_int(b, num_required_elements);
    //             l3_warmer_int(idx, num_required_elements);
    //             l3_warmer_int(cond, num_required_elements);
    //             l3_warmer_int(boundaries, num_required_elements + 1);
    //         } else if (dist != "allmiss") {
    //             gather_rmw_directrangeloop_cond(boundaries, a2, b, idx, min, max, 3, cond, 128);
    //         }
    //         gather_rmw_directrangeloop_cond_maa(boundaries, a2, b, idx, min, max, 3, cond, 128);
    //     }
    //     if (base) {
    //         if (dist == "allhitl3") {
    //             l3_warmer_int(a2, num_required_elements);
    //             l3_warmer_int(b, num_required_elements);
    //             l3_warmer_int(idx, num_required_elements);
    //             l3_warmer_int(cond, num_required_elements);
    //             l3_warmer_int(boundaries, num_required_elements + 1);
    //         } else if (dist != "allmiss") {
    //             gather_rmw_directrangeloop_cond(boundaries, a1, b, idx, min, max, 3, cond, 128);
    //         }
    //         gather_rmw_directrangeloop_cond(boundaries, a1, b, idx, min, max, 3, cond, 128);
    //     }
    //     if (cmp) {
    //         for (int i = 0; i < n; i++) {
    //             if (abs(a1[i] - a2[i]) >= DELTA) {
    //                 cout << "Error -- gather_rmw_directrangeloop_cond: " << i << " " << a1[i] << " " << a2[i] << endl;
    //                 free(nodes);
    //                 free(a1);
    //                 free(a2);
    //                 free(b);
    //                 free(idx);
    //                 free(boundaries);
    //                 free(cond);
    //                 return 1;
    //             }
    //         }
    //         cout << "gather_rmw_directrangeloop_cond correct" << std::endl;
    //     }
    // }

    if (kernel == "gather_directrangeloop_indircond" || kernel == "all") {
        if (maa) {
            if (dist == "allhitl3") {
                l3_warmer_int(a2, num_required_elements);
                l3_warmer_int(b, num_required_elements);
                l3_warmer_int(idx, num_required_elements);
                l3_warmer_int(cond, num_required_elements);
                l3_warmer_int(boundaries, num_required_elements + 1);
            } else if (dist != "allmiss") {
                // for warming up a, idx, and boundaries in correct cores
                gather_directrangeloop_indircond_maa(boundaries, a2, b, idx, min, max, 3, cond, 128);
                // for warming up b in LLC
                l3_warmer_int(b, num_required_elements);
                // for warming up cond in LLC
                l3_warmer_int(cond, num_required_elements);
            }
            gather_directrangeloop_indircond_maa(boundaries, a2, b, idx, min, max, 3, cond, 128);
        }
        if (base) {
            if (dist == "allhitl3") {
                l3_warmer_int(a1, num_required_elements);
                l3_warmer_int(b, num_required_elements);
                l3_warmer_int(idx, num_required_elements);
                l3_warmer_int(cond, num_required_elements);
                l3_warmer_int(boundaries, num_required_elements + 1);
            } else if (dist != "allmiss") {
                gather_directrangeloop_indircond(boundaries, a1, b, idx, min, max, 3, cond, 128);
            }
            gather_directrangeloop_indircond(boundaries, a1, b, idx, min, max, 3, cond, 128);
        }
        if (cmp) {
            for (int i = 0; i < n; i++) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- gather_directrangeloop_indircond: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(a1);
                    free(a2);
                    free(b);
                    free(idx);
                    free(boundaries);
                    free(cond);
                    return 1;
                }
            }
            cout << "gather_rmw_directrangeloop_cond correct" << std::endl;
        }
    }

    //     if (kernel == "gather_rmw_indirectrangeloop_cond" || kernel == "all") {
    //         min = 0;
    //         max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    //         if (maa) {
    //             gather_rmw_indirectrangeloop_cond_maa(nodes, boundaries, a2, b, idx, min, max, C, cond, 128);
    //         }
    //         if (base) {
    //             gather_rmw_indirectrangeloop_cond(nodes, boundaries, a1, b, idx, min, max, C, cond, 128);
    //         }
    //         if (cmp) {
    //             for (int i = 0; i < (long)n * S; i++) {
    //                 if (abs(a1[i] - a2[i]) >= DELTA) {
    //                     cout << "Error -- gather_rmw_indirectrangeloop_cond: " << i << " " << a1[i] << " " << a2[i] << endl;
    //                     free(nodes);
    //                     free(a1);
    //                     free(a2);
    //                     free(b);
    //                     free(idx);
    //                     return 1;
    //                 }
    //             }
    //             cout << "gather_rmw_indirectrangeloop_cond correct" << std::endl;
    //         }
    //     }

    //     if (kernel == "gather_rmw_cond_indirectrangeloop_cond" || kernel == "all") {
    //         min = 0;
    //         max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    //         if (maa) {
    //             gather_rmw_cond_indirectrangeloop_cond_maa(nodes, boundaries, a2, b, idx, min, max, C, cond, 128);
    //         }
    //         if (base) {
    //             gather_rmw_cond_indirectrangeloop_cond(nodes, boundaries, a1, b, idx, min, max, C, cond, 128);
    //         }
    //         if (cmp) {
    //             for (int i = 0; i < (long)n * S; i++) {
    //                 if (abs(a1[i] - a2[i]) >= DELTA) {
    //                     cout << "Error -- gather_rmw_cond_indirectrangeloop_cond: " << i << " " << a1[i] << " " << a2[i] << endl;
    //                     free(nodes);
    //                     free(a1);
    //                     free(a2);
    //                     free(b);
    //                     free(idx);
    //                     return 1;
    //                 }
    //             }
    //             cout << "gather_rmw_cond_indirectrangeloop_cond correct" << std::endl;
    //         }
    //     }

    //     if (kernel == "gather_rmw_indirectcond_indirectrangeloop_indirectcond" || kernel == "all") {
    //         min = 0;
    //         max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    //         if (maa) {
    //             gather_rmw_indirectcond_indirectrangeloop_indirectcond_maa(nodes, boundaries, a2, b, idx, min, max, C, cond, 128);
    //         }
    //         if (base) {
    //             gather_rmw_indirectcond_indirectrangeloop_indirectcond(nodes, boundaries, a1, b, idx, min, max, C, cond, 128);
    //         }
    //         if (cmp) {
    //             for (int i = 0; i < (long)n * S; i++) {
    //                 if (abs(a1[i] - a2[i]) >= DELTA) {
    //                     cout << "Error -- gather_rmw_indirectcond_indirectrangeloop_indirectcond: " << i << " " << a1[i] << " " << a2[i] << endl;
    //                     free(nodes);
    //                     free(a1);
    //                     free(a2);
    //                     free(b);
    //                     free(idx);
    //                     return 1;
    //                 }
    //             }
    //             cout << "gather_rmw_indirectcond_indirectrangeloop_indirectcond correct" << std::endl;
    //         }
    //     }
    // #ifdef COMPILER_TEST
    //     cout << "LOG: Start Compiler Tests" << endl;
    //     min = 0;
    //     max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    //     gather_compiler(a1, b, idx, min, max, C);
    //     gather(a2, b, idx, min, max, C);
    //     for (int i = 0; i < (long)n * S; i++) {
    //         if (abs(a1[i] - a2[i]) >= DELTA) {
    //             cout << "Error -- Gather Compiler: " << i << " " << a1[i] << " " << a2[i] << endl;
    //             free(nodes);
    //             free(a1);
    //             free(a2);
    //             free(b);
    //             free(idx);
    //             return 1;
    //         }
    //     }
    //     cout << "gather_compiler correct" << std::endl;

    //     min = 0;
    //     max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    //     scatter_compiler(a1, b, idx, min, max, C);
    //     scatter(a2, b, idx, min, max, C);
    //     for (int i = 0; i < (long)n * S; i++) {
    //         if (abs(a1[i] - a2[i]) >= DELTA) {
    //             cout << "Error -- Scatter Compiler: " << i << " " << a1[i] << " " << a2[i] << endl;
    //             free(nodes);
    //             free(a1);
    //             free(a2);
    //             free(b);
    //             free(idx);
    //             return 1;
    //         }
    //     }
    //     cout << "scatter_compiler correct" << std::endl;

    //     min = 0;
    //     max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    //     rmw_compiler(a1, b, idx, min, max, C);
    //     rmw(a2, b, idx, min, max, C);
    //     for (int i = 0; i < (long)n * S; i++) {
    //         if (abs(a1[i] - a2[i]) >= DELTA) {
    //             cout << "Error -- rmw Compiler: " << i << " " << a1[i] << " " << a2[i] << endl;
    //             free(nodes);
    //             free(a1);
    //             free(a2);
    //             free(b);
    //             free(idx);
    //             return 1;
    //         }
    //     }
    //     cout << "rmw_compiler correct" << std::endl;

    //     min = 0;
    //     max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    //     gather_scatter_compiler(a1, b, idx, min, max, C);
    //     gather_scatter(a2, b, idx, min, max, C);
    //     for (int i = 0; i < (long)n * S; i++) {
    //         if (abs(a1[i] - a2[i]) >= DELTA) {
    //             cout << "Error -- gather_scatter Compiler: " << i << " " << a1[i] << " " << a2[i] << endl;
    //             free(nodes);
    //             free(a1);
    //             free(a2);
    //             free(b);
    //             free(idx);
    //             return 1;
    //         }
    //     }
    //     cout << "gather_scatter_compiler correct" << std::endl;

    //     min = 0;
    //     max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    //     gather_rmw_compiler(a1, b, idx, min, max, C);
    //     gather_rmw(a2, b, idx, min, max, C);
    //     for (int i = 0; i < (long)n * S; i++) {
    //         if (abs(a1[i] - a2[i]) >= DELTA) {
    //             cout << "Error -- gather_rmw Compiler: " << i << " " << a1[i] << " " << a2[i] << endl;
    //             free(nodes);
    //             free(a1);
    //             free(a2);
    //             free(b);
    //             free(idx);
    //             return 1;
    //         }
    //     }
    //     cout << "gather_rmw_compiler correct" << std::endl;

    //     min = 0;
    //     max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    //     gather_rmw_cond_compiler(a1, b, idx, min, max, C, cond, 128);
    //     gather_rmw_cond(a2, b, idx, min, max, C, cond, 128);
    //     for (int i = 0; i < (long)n * S; i++) {
    //         if (abs(a1[i] - a2[i]) >= DELTA) {
    //             cout << "Error -- gather_rmw_cond Compiler: " << i << " " << a1[i] << " " << a2[i] << endl;
    //             free(nodes);
    //             free(a1);
    //             free(a2);
    //             free(b);
    //             free(idx);
    //             return 1;
    //         }
    //     }
    //     cout << "gather_rmw_cond_compiler correct" << std::endl;
    // #endif
    std::cout << "End of Test, all tests correct!" << std::endl;
#ifdef GEM5
    m5_exit(0);
#endif
    free(nodes);
    free(a1);
    free(a2);
    free(b);
    free(idx);
    return 0;
}