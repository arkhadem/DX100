
#include "MAA.hpp"
#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <random>
#include <omp.h>

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

void gather_stream(float *a, float *b, int *idx, int min, int max, const int C) {
    std::cout << "starting gather_stream min(" << min << "), max(" << max << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
#pragma omp for simd
    for (int i = min; i < max; i++) {
        a[i] = C * b[i];
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

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
    float *a_s;
    #pragma omp single nowait
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
    // Here we change a loop from min to max with 1 to
    // a loop from 0 to i_max
    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base + min;
        #pragma omp single nowait
    {
        // i = min
        maa_const(curr_min, min_reg_id);
        // idx[i]
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        // b[idx[i]]
        maa_indirect_load<float>(b, idx_tile, b_tile);
        a_s = &(a[min + i_base]);
        // wait until the b tile is fetched to the SPD
        wait_ready(b_tile);
    }
#ifdef GEM5
        m5_dump_stats(0, 0);
        m5_work_end(0, 0);
        m5_work_begin(0, 0);
        m5_reset_stats(0, 0);
#endif
#pragma omp for simd aligned(a_s, b_p: 16) simdlen(4)
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
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
}


void scatter(float *a, float *b, int *idx, int min, int max, const int C) {
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
void scatter_maa(float *a, float *b, int *idx, int min, int max, const int C) {
    std::cout << "starting scatter_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(1);
    int C_reg_id = get_new_reg<float>(C);

    int idx_tile = get_new_tile<int>();
    int a_tile = get_new_tile<float>();
    int b_tile = get_new_tile<float>();

    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base + min;
        wait_ready(idx_tile);
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        maa_stream_load<float>(b, min_reg_id, max_reg_id, stride_reg_id, b_tile);
        maa_alu_scalar<float>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP);
        maa_indirect_store<float>(a, idx_tile, a_tile);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void rmw(float *a, float *b, int *idx, int min, int max, const int C) {
    std::cout << "starting rmw min(" << min << "), max(" << max << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
// TODO: parallelize
    for (int i = min; i < max; i++) {
        a[idx[i]] += C * b[i];
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}
void rmw_maa(float *a, float *b, int *idx, int min, int max, const int C) {
    std::cout << "starting rmw_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(1);
    int C_reg_id = get_new_reg<float>(C);

    int idx_tile = get_new_tile<int>();
    int a_tile = get_new_tile<float>();
    int b_tile = get_new_tile<float>();

    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base + min;
        wait_ready(idx_tile);
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        maa_stream_load<float>(b, min_reg_id, max_reg_id, stride_reg_id, b_tile);
        maa_alu_scalar<float>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP);
        maa_indirect_rmw<float>(a, idx_tile, a_tile, Operation_t::ADD_OP);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_scatter(float *a, float *b, int *idx, int min, int max, const int C) {
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
void gather_scatter_maa(float *a, float *b, int *idx, int min, int max, const int C) {
    std::cout << "starting gather_scatter_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(1);
    int C_reg_id = get_new_reg<float>(C);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<float>();
    int a_tile = get_new_tile<float>();

    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base + min;
        wait_ready(idx_tile);
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        maa_indirect_load<float>(b, idx_tile, b_tile);
        maa_alu_scalar<float>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP);
        maa_indirect_store<float>(a, idx_tile, a_tile);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw(float *a, float *b, int *idx, int min, int max, const int C) {
    std::cout << "starting gather_rmw min(" << min << "), max(" << max << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
// TODO: parallelize
    for (int i = min; i < max; i++) {
        a[idx[i]] += C * b[idx[i]];
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}
void gather_rmw_maa(float *a, float *b, int *idx, int min, int max, const int C) {
    std::cout << "starting maa_gather_rmw min(" << min << "), max(" << max << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(1);
    int C_reg_id = get_new_reg<float>(C);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<float>();
    int a_tile = get_new_tile<float>();

    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base + min;
        wait_ready(idx_tile);
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        maa_indirect_load<float>(b, idx_tile, b_tile);
        maa_alu_scalar<float>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP);
        maa_indirect_rmw<float>(a, idx_tile, a_tile, Operation_t::ADD_OP);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_cond(float *a, float *b, int *idx, int min, int max, const float C, float *cond, const float cond_const) {
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
void gather_rmw_cond_maa(float *a, float *b, int *idx, int min, int max, const float C, float *cond, const float cond_const) {
    std::cout << "starting gather_rmw_cond_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(1);
    int C_reg_id = get_new_reg<float>(C);

    int cond_const_reg_id = get_new_reg<float>(cond_const);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<float>();
    int a_tile = get_new_tile<float>();
    int cond_tile = get_new_tile<float>();
    int cond_res_tile = get_new_tile<uint32_t>();

    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base + min;
        wait_ready(cond_tile);
        maa_const(curr_min, min_reg_id);
        maa_stream_load<float>(cond, min_reg_id, max_reg_id, stride_reg_id, cond_tile);
        maa_alu_scalar<float>(cond_tile, cond_const_reg_id, cond_res_tile, Operation_t::GT_OP);
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile, cond_res_tile);
        maa_indirect_load<float>(b, idx_tile, b_tile, cond_res_tile);
        maa_alu_scalar<float>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP, cond_res_tile);
        maa_indirect_rmw<float>(a, idx_tile, a_tile, Operation_t::ADD_OP, cond_res_tile);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_directrangeloop_cond(int *boundaries, float *a, float *b, int *idx, int min, int max, const float C, float *cond, const float cond_const) {
    std::cout << "starting gather_rmw_directrangeloop_cond min(" << min << "), max(" << max << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
// TODO: parallelize
    for (int i = min; i < max; i++) {
        for (int j = boundaries[i]; j < boundaries[i + 1]; j++) {
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
void gather_rmw_directrangeloop_cond_maa(int *boundaries, float *a, float *b, int *idx, int min, int max, const float C, float *cond, const float cond_const) {
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
    int C_reg_id = get_new_reg<float>(C);

    int cond_const_reg_id = get_new_reg<float>(cond_const);
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<float>();
    int a_tile = get_new_tile<float>();
    int cond_tile = get_new_tile<float>();
    int boundaries0_tile = get_new_tile<int>();
    int boundaries1_tile = get_new_tile<int>();
    int i_tile = get_new_tile<int>();
    int j_tile = get_new_tile<int>();
    int cond_res_tile = get_new_tile<uint32_t>();

    float *b_p = get_cacheable_tile_pointer<float>(b_tile);
    float *a_p = get_cacheable_tile_pointer<float>(a_tile);
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
            maa_range_loop<float>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile);
            maa_indirect_load<float>(cond, j_tile, cond_tile);
            maa_alu_scalar<float>(cond_tile, cond_const_reg_id, cond_res_tile, Operation_t::GT_OP);
            maa_indirect_load<int>(idx, j_tile, idx_tile, cond_res_tile);
            maa_indirect_load<float>(b, idx_tile, b_tile, cond_res_tile);
            maa_alu_scalar<float>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP, cond_res_tile);
            maa_indirect_rmw<float>(a, idx_tile, a_tile, Operation_t::ADD_OP, cond_res_tile);
            wait_ready(j_tile);
            curr_tilej_size = get_tile_size(j_tile);
        } while (curr_tilej_size > 0);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_indirectrangeloop_cond(int *nodes, int *boundaries, float *a, float *b, int *idx, int min, int max, const float C, float *cond, const float cond_const) {
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

void gather_rmw_indirectrangeloop_cond_maa(int *nodes, int *boundaries, float *a, float *b, int *idx, int min, int max, const float C, float *cond, const float cond_const) {
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
    int C_reg_id = get_new_reg<float>(C);

    int cond_const_reg_id = get_new_reg<float>(cond_const);
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<float>();
    int a_tile = get_new_tile<float>();
    int cond_tile = get_new_tile<float>();
    int boundaries0_tile = get_new_tile<int>();
    int boundaries1_tile = get_new_tile<int>();
    int i_tile = get_new_tile<int>();
    int j_tile = get_new_tile<int>();
    int nodes_tile = get_new_tile<int>();
    int cond_res_tile = get_new_tile<uint32_t>();

    float *b_p = get_cacheable_tile_pointer<float>(b_tile);
    float *a_p = get_cacheable_tile_pointer<float>(a_tile);
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
            maa_range_loop<float>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile);
            maa_indirect_load<float>(cond, j_tile, cond_tile);
            maa_alu_scalar<float>(cond_tile, cond_const_reg_id, cond_res_tile, Operation_t::GT_OP);
            maa_indirect_load<int>(idx, j_tile, idx_tile, cond_res_tile);
            maa_indirect_load<float>(b, idx_tile, b_tile, cond_res_tile);
            maa_alu_scalar<float>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP, cond_res_tile);
            maa_indirect_rmw<float>(a, idx_tile, a_tile, Operation_t::ADD_OP, cond_res_tile);
            wait_ready(j_tile);
            curr_tilej_size = get_tile_size(j_tile);
        } while (curr_tilej_size > 0);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_cond_indirectrangeloop_cond(int *nodes, int *boundaries, float *a, float *b, int *idx, int min, int max, const float C, float *cond, const float cond_const) {
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

void gather_rmw_cond_indirectrangeloop_cond_maa(int *nodes, int *boundaries, float *a, float *b, int *idx, int min, int max, const float C, float *cond, const float cond_const) {
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
    int C_reg_id = get_new_reg<float>(C);

    int cond_const_reg_id = get_new_reg<float>(cond_const);
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<float>();
    int a_tile = get_new_tile<float>();
    int condi_tile = get_new_tile<float>();
    int condj_tile = get_new_tile<float>();
    int boundaries0_tile = get_new_tile<int>();
    int boundaries1_tile = get_new_tile<int>();
    int i_tile = get_new_tile<int>();
    int j_tile = get_new_tile<int>();
    int nodes_tile = get_new_tile<int>();
    int condi_res_tile = get_new_tile<uint32_t>();
    int condj_res_tile = get_new_tile<uint32_t>();

    float *b_p = get_cacheable_tile_pointer<float>(b_tile);
    float *a_p = get_cacheable_tile_pointer<float>(a_tile);
    int *i_p = get_cacheable_tile_pointer<int>(i_tile);
    int *j_p = get_cacheable_tile_pointer<int>(j_tile);
    uint32_t *condj_res_p = get_cacheable_tile_pointer<uint32_t>(condj_res_tile);

    int i_max = max - min;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base + min;
        maa_const(curr_min, min_reg_id);
        maa_stream_load<float>(cond, min_reg_id, max_reg_id, stride_reg_id, condi_tile);
        maa_alu_scalar<float>(condi_tile, cond_const_reg_id, condi_res_tile, Operation_t::GT_OP);
        maa_stream_load<int>(nodes, min_reg_id, max_reg_id, stride_reg_id, nodes_tile, condi_res_tile);
        maa_indirect_load<int>(boundaries, nodes_tile, boundaries0_tile, condi_res_tile);
        maa_indirect_load<int>(&boundaries[1], nodes_tile, boundaries1_tile, condi_res_tile);

        int curr_tilej_size = 0;
        maa_const<int>(0, last_i_reg_id);
        maa_const<int>(-1, last_j_reg_id);
        do {
            maa_range_loop<float>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile, condi_res_tile);
            maa_indirect_load<float>(cond, j_tile, condj_tile);
            maa_alu_scalar<float>(condj_tile, cond_const_reg_id, condj_res_tile, Operation_t::GT_OP);
            maa_indirect_load<int>(idx, j_tile, idx_tile, condj_res_tile);
            maa_indirect_load<float>(b, idx_tile, b_tile, condj_res_tile);
            maa_alu_scalar<float>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP, condj_res_tile);
            maa_indirect_rmw<float>(a, idx_tile, a_tile, Operation_t::ADD_OP, condj_res_tile);
            wait_ready(j_tile);
            curr_tilej_size = get_tile_size(j_tile);
        } while (curr_tilej_size > 0);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_indirectcond_indirectrangeloop_indirectcond(int *nodes, int *boundaries, float *a, float *b, int *idx, int min, int max, const float C, float *cond, const float cond_const) {
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

void gather_rmw_indirectcond_indirectrangeloop_indirectcond_maa(int *nodes, int *boundaries, float *a, float *b, int *idx, int min, int max, const float C, float *cond, const float cond_const) {
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
    int C_reg_id = get_new_reg<float>(C);

    int cond_const_reg_id = get_new_reg<float>(cond_const);
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<float>();
    int a_tile = get_new_tile<float>();
    int condi_tile = get_new_tile<float>();
    int condj_tile = get_new_tile<float>();
    int boundaries0_tile = get_new_tile<int>();
    int boundaries1_tile = get_new_tile<int>();
    int i_tile = get_new_tile<int>();
    int j_tile = get_new_tile<int>();
    int nodesi_tile = get_new_tile<int>();
    int nodesj_tile = get_new_tile<int>();
    int condi_res_tile = get_new_tile<uint32_t>();
    int condj_res_tile = get_new_tile<uint32_t>();

    float *b_p = get_cacheable_tile_pointer<float>(b_tile);
    float *a_p = get_cacheable_tile_pointer<float>(a_tile);
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
        maa_indirect_load<float>(cond, nodesi_tile, condi_tile);
        maa_alu_scalar<float>(condi_tile, cond_const_reg_id, condi_res_tile, Operation_t::GT_OP);
        maa_indirect_load<int>(boundaries, nodesi_tile, boundaries0_tile, condi_res_tile);
        maa_indirect_load<int>(&boundaries[1], nodesi_tile, boundaries1_tile, condi_res_tile);

        int curr_tilej_size = 0;
        maa_const<int>(0, last_i_reg_id);
        maa_const<int>(-1, last_j_reg_id);
        do {
            maa_range_loop<float>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile, condi_res_tile);
            maa_indirect_load<int>(nodes, j_tile, nodesj_tile);
            maa_indirect_load<float>(cond, nodesj_tile, condj_tile);
            maa_alu_scalar<float>(condj_tile, cond_const_reg_id, condj_res_tile, Operation_t::GT_OP);
            maa_indirect_load<int>(idx, j_tile, idx_tile, condj_res_tile);
            maa_indirect_load<float>(b, idx_tile, b_tile, condj_res_tile);
            maa_alu_scalar<float>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP, condj_res_tile);
            maa_indirect_rmw<float>(a, idx_tile, a_tile, Operation_t::ADD_OP, condj_res_tile);
            wait_ready(j_tile);
            curr_tilej_size = get_tile_size(j_tile);
        } while (curr_tilej_size > 0);
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
#pragma omp for 
    for (int i = 0; i < 1024; i++) {
        std::cout << "iter" << i << std::endl;
        for (int j = 0; j < 3024; j++) {
            p[i * 3024 + j] = rand();
        }
    }
    std::cout << "cache flushed" << std::endl;
}

/*******************************************************************************/
/*******************************************************************************/
/*                                    MAIN                                     */
/*******************************************************************************/
/*******************************************************************************/

int main(int argc, char *argv[]) {

    if (argc != 5) {
        cout << "Usage: " << argv[0] << " <n> <d> [BASE|MAA|CMP] [kernel|all]" << endl;
        exit(1);
    }

    srand((unsigned)time(NULL));

    int n = stoi(argv[1]);
    int d = stoi(argv[2]);
    string mode = argv[3];
    string kernel = argv[4];
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
        cout << "Usage: " << argv[0] << " <n> <d> [BASE|MAA|CMP]" << endl;
        exit(1);
    }
    int S = 4;
    const int C = 3;

    int *nodes = (int *)malloc(sizeof(int) * S * n);
    int *boundaries = (int *)malloc(sizeof(int) * (n + 1));
    float *a1 = (float *)malloc(sizeof(float) * S * n);
    float *a2 = (float *)malloc(sizeof(float) * S * n);
    float *cond = (float *)malloc(sizeof(float) * S * n);
    float *b = (float *)malloc(sizeof(float) * S * n);
    int *idx = (int *)malloc(sizeof(int) * S * n);

    std::cout << "initializing general arrays" << std::endl;
    // long long total_distance = 0;
    for (long i = 0; i < (long)n * S; i++) {
        a1[i] = a2[i] = rand() % 1024;
        cond[i] = rand() % 1024;
        b[i] = rand() % 1024;
        int tmp = rand() % (2 * d) % (n * S);
        // idx[i] = (i - tmp) < 0 ? tmp : (i - tmp);
        idx[i] = i;
        // total_distance += abs(i - idx[i]);
        tmp = rand() % (2 * d) % (n);
        nodes[i] = (i - tmp) < 0 ? tmp : (i - tmp);
    }
    // std::cout << "average_distance: " << total_distance / (n * S) << std::endl;
    int num_nodes = 1;
    boundaries[0] = 0;

    std::cout << "initializing boundaries" << std::endl;
    for (; num_nodes < n; num_nodes++) {
        int tmp = S + ((rand() % (2 * S)) - S);
        assert(tmp >= 0 && tmp < 2 * S);
        boundaries[num_nodes] = boundaries[num_nodes - 1] + tmp;
        if (boundaries[num_nodes] >= S * n) {
            break;
        }
    }
    boundaries[num_nodes] = S * n;

    int min = 0;         // rand() % (num_nodes / 8);
    int max = num_nodes; // - rand() % (num_nodes / 8);

#ifdef GEM5
    std::cout << "Checkpointing started" << std::endl;
    m5_checkpoint(0, 0);
    std::cout << "Checkpointing ended" << std::endl;
#endif

    std::cout << "initializing done, testing..." << std::endl;

    if (maa) {
        alloc_MAA();
    }

#ifdef GEM5
    m5_add_mem_region((void *)a1, (void *)(&a1[S * n]), 6);
    m5_add_mem_region((void *)a2, (void *)(&a2[S * n]), 7);
    m5_add_mem_region((void *)b, (void *)(&b[S * n]), 8);
    m5_add_mem_region((void *)idx, (void *)(&idx[S * n]), 9);
#endif
    #pragma omp parallel
    {

        // Master thread can print or perform other tasks if needed
    #pragma omp master
    {
        std::cout << "Parallel region started with " << omp_get_num_threads() << " threads." << std::endl;
    }

    // Execute kernel functions within the parallel region
    #pragma omp single nowait
    {
    if (kernel == "gather_stream" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        if (base) {
            gather_stream(a1, b, idx, min, max, C);
            gather_stream(a1, b, idx, min, max, C);
        }
    }
    if (kernel == "gather" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        if (maa) {
            gather(a1, b, idx, min, max, C);
            gather_maa(a2, b, idx, min, max, C);
        }
        if (base) {
            gather(a1, b, idx, min, max, C);
            gather(a1, b, idx, min, max, C);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i++) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- Gather: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    exit(1);
                }
            }
            cout << "gather correct" << std::endl;
        }
    }

    if (kernel == "scatter" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        if (maa) {
            scatter_maa(a2, b, idx, min, max, C);
        }
        if (base) {
            scatter(a1, b, idx, min, max, C);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i++) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- Scatter: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    exit(1);
                }
            }
            cout << "scatter correct" << std::endl;
        }
    }

    if (kernel == "rmw" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        if (maa) {
            rmw_maa(a2, b, idx, min, max, C);
        }
        if (base) {
            rmw(a1, b, idx, min, max, C);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i++) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- rmw: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    exit(1);
                }
            }
            cout << "rmw correct" << std::endl;
        }
    }

    if (kernel == "gather_scatter" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        if (maa) {
            gather_scatter_maa(a2, b, idx, min, max, C);
        }
        if (base) {
            gather_scatter(a1, b, idx, min, max, C);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i++) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- gather_scatter: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    exit(1);
                }
            }
            cout << "gather_scatter correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        if (maa) {
            gather_rmw_maa(a2, b, idx, min, max, C);
        }
        if (base) {
            gather_rmw(a1, b, idx, min, max, C);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i++) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- gather_rmw: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    exit(1);
                }
            }
            cout << "gather_rmw correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw_cond" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        if (maa) {
            gather_rmw_cond_maa(a2, b, idx, min, max, C, cond, 128);
        }
        if (base) {
            gather_rmw_cond(a1, b, idx, min, max, C, cond, 128);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i++) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- gather_rmw_cond: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    exit(1);
                }
            }
            cout << "gather_rmw_cond correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw_directrangeloop_cond" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        if (maa) {
            gather_rmw_directrangeloop_cond_maa(boundaries, a2, b, idx, min, max, C, cond, 128);
        }
        if (base) {
            gather_rmw_directrangeloop_cond(boundaries, a1, b, idx, min, max, C, cond, 128);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i++) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- gather_rmw_directrangeloop_cond: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    exit(1);
                }
            }
            cout << "gather_rmw_directrangeloop_cond correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw_indirectrangeloop_cond" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        if (maa) {
            gather_rmw_indirectrangeloop_cond_maa(nodes, boundaries, a2, b, idx, min, max, C, cond, 128);
        }
        if (base) {
            gather_rmw_indirectrangeloop_cond(nodes, boundaries, a1, b, idx, min, max, C, cond, 128);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i++) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- gather_rmw_indirectrangeloop_cond: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    exit(1);
                }
            }
            cout << "gather_rmw_indirectrangeloop_cond correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw_cond_indirectrangeloop_cond" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        if (maa) {
            gather_rmw_cond_indirectrangeloop_cond_maa(nodes, boundaries, a2, b, idx, min, max, C, cond, 128);
        }
        if (base) {
            gather_rmw_cond_indirectrangeloop_cond(nodes, boundaries, a1, b, idx, min, max, C, cond, 128);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i++) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- gather_rmw_cond_indirectrangeloop_cond: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    exit(1);
                }
            }
            cout << "gather_rmw_cond_indirectrangeloop_cond correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw_indirectcond_indirectrangeloop_indirectcond" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        if (maa) {
            gather_rmw_indirectcond_indirectrangeloop_indirectcond_maa(nodes, boundaries, a2, b, idx, min, max, C, cond, 128);
        }
        if (base) {
            gather_rmw_indirectcond_indirectrangeloop_indirectcond(nodes, boundaries, a1, b, idx, min, max, C, cond, 128);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i++) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- gather_rmw_indirectcond_indirectrangeloop_indirectcond: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    exit(1);
                }
            }
            cout << "gather_rmw_indirectcond_indirectrangeloop_indirectcond correct" << std::endl;
        }
      }
    } // omp single
  } // omp parallel
#ifdef COMPILER_TEST
    cout << "LOG: Start Compiler Tests" << endl;
    min = 0;         // rand() % (num_nodes / 8);
    max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    gather_compiler(a1, b, idx, min, max, C);
    gather(a2, b, idx, min, max, C);
    for (int i = 0; i < (long)n * S; i++) {
        if (abs(a1[i] - a2[i]) >= DELTA) {
            cout << "Error -- Gather Compiler: " << i << " " << a1[i] << " " << a2[i] << endl;
            free(nodes);
            free(boundaries);
            free(a1);
            free(a2);
            free(cond);
            free(b);
            free(idx);
            exit(1);
        }
    }
    cout << "gather_compiler correct" << std::endl;

    min = 0;         // rand() % (num_nodes / 8);
    max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    scatter_compiler(a1, b, idx, min, max, C);
    scatter(a2, b, idx, min, max, C);
    for (int i = 0; i < (long)n * S; i++) {
        if (abs(a1[i] - a2[i]) >= DELTA) {
            cout << "Error -- Scatter Compiler: " << i << " " << a1[i] << " " << a2[i] << endl;
            free(nodes);
            free(boundaries);
            free(a1);
            free(a2);
            free(cond);
            free(b);
            free(idx);
            exit(1);
        }
    }
    cout << "scatter_compiler correct" << std::endl;

    min = 0;         // rand() % (num_nodes / 8);
    max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    rmw_compiler(a1, b, idx, min, max, C);
    rmw(a2, b, idx, min, max, C);
    for (int i = 0; i < (long)n * S; i++) {
        if (abs(a1[i] - a2[i]) >= DELTA) {
            cout << "Error -- rmw Compiler: " << i << " " << a1[i] << " " << a2[i] << endl;
            free(nodes);
            free(boundaries);
            free(a1);
            free(a2);
            free(cond);
            free(b);
            free(idx);
            exit(1);
        }
    }
    cout << "rmw_compiler correct" << std::endl;

    min = 0;         // rand() % (num_nodes / 8);
    max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    gather_scatter_compiler(a1, b, idx, min, max, C);
    gather_scatter(a2, b, idx, min, max, C);
    for (int i = 0; i < (long)n * S; i++) {
        if (abs(a1[i] - a2[i]) >= DELTA) {
            cout << "Error -- gather_scatter Compiler: " << i << " " << a1[i] << " " << a2[i] << endl;
            free(nodes);
            free(boundaries);
            free(a1);
            free(a2);
            free(cond);
            free(b);
            free(idx);
            exit(1);
        }
    }
    cout << "gather_scatter_compiler correct" << std::endl;

    min = 0;         // rand() % (num_nodes / 8);
    max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    gather_rmw_compiler(a1, b, idx, min, max, C);
    gather_rmw(a2, b, idx, min, max, C);
    for (int i = 0; i < (long)n * S; i++) {
        if (abs(a1[i] - a2[i]) >= DELTA) {
            cout << "Error -- gather_rmw Compiler: " << i << " " << a1[i] << " " << a2[i] << endl;
            free(nodes);
            free(boundaries);
            free(a1);
            free(a2);
            free(cond);
            free(b);
            free(idx);
            exit(1);
        }
    }
    cout << "gather_rmw_compiler correct" << std::endl;

    min = 0;         // rand() % (num_nodes / 8);
    max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    gather_rmw_cond_compiler(a1, b, idx, min, max, C, cond, 128);
    gather_rmw_cond(a2, b, idx, min, max, C, cond, 128);
    for (int i = 0; i < (long)n * S; i++) {
        if (abs(a1[i] - a2[i]) >= DELTA) {
            cout << "Error -- gather_rmw_cond Compiler: " << i << " " << a1[i] << " " << a2[i] << endl;
            free(nodes);
            free(boundaries);
            free(a1);
            free(a2);
            free(cond);
            free(b);
            free(idx);
            exit(1);
        }
    }
    cout << "gather_rmw_cond_compiler correct" << std::endl;
#endif
    std::cout << "End of Test, all tests correct!" << std::endl;
#ifdef GEM5
    m5_exit(0);
#endif
    free(nodes);
    free(boundaries);
    free(a1);
    free(a2);
    free(cond);
    free(b);
    free(idx);
    return 0;
}