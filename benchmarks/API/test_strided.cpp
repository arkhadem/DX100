
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
void gather_rmw_cond_compiler(float *a, float *b, int *idx, int min, int max, const float mul_const, float *cond, const float cond_const);
#endif
// void mmio_test() {
//     init_MAA();
//     int tile_id = 5;
//     uint32_t val;
//     // noncacheable write
//     volatile uint32_t *tile_nc = get_cacheable_tile_pointer<uint32_t>(tile_id);
//     tile_nc[7] = 23;
//     // cacheable read
//     uint32_t *tile_c = get_cacheable_tile_pointer<uint32_t>(tile_id);
//     val = tile_c[7];
//     assert(val == 23);
//     // cacheable read again
//     val = tile_c[7];
//     assert(val == 23);
//     // read size
//     int tile_size = get_tile_size(tile_id);
//     assert(tile_size == 0);
//     // read ready
//     int tile_ready = get_tile_ready(tile_id);
//     assert(tile_ready == 0);
//     // noncacheable write to register
//     int reg_id = 3;
//     set_reg(reg_id, 23);
//     // noncacheable read from register
//     int reg_val = get_reg<int>(reg_id);
//     assert(reg_val == 23);
//     // sending instruction
//     int *data = (int *)0x111000111000;
//     maa_stream_load<int>(data, 1, 2, 3, 4, 5);
// }
// void stream_load(int *a, int min, int max, int stride) {
//     std::cout << "starting gather min(" << min << "), max(" << max << "), stride(" << stride << ")" << std::endl;
//     init_MAA();

//     // max scalar register
//     int max_reg_id = get_new_reg<int>(max);
//     // min scalar register
//     int min_reg_id = get_new_reg<int>(min);
//     // stride scalar register
//     int stride_reg_id = get_new_reg<int>(stride);

//     // allocate a tile of a[i]
//     int a_tile = get_new_tile<int>();
//     int *a_p = get_cacheable_tile_pointer<int>(a_tile);

//     maa_stream_load<int>(a, min_reg_id, max_reg_id, stride_reg_id, a_tile);
//     std::cout << "instruction sent, waiting for ready" << std::endl;
//     wait_ready(a_tile);
//     std::cout << "Ready received, reading data" << std::endl;

//     bool correct = true;
//     for (int i = min, idx = 0; i < max; i += stride, idx++) {
//         if (a_p[idx] != a[i]) {
//             cout << "Error -- stream_load: " << i << " " << a_p[idx] << " " << a[i] << std::endl;
// #ifdef GEM5
//             m5_exit(0);
// #else
//             correct = false;
// #endif
//         }
//     }
//     if (correct)
//         cout << "stream_load correct" << std::endl;
// }

void gather(float *a, float *b, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting gather min(" << min << "), max(" << max << "), stride(" << stride << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
#pragma omp parallel for simd
    for (int i = min; i < max; i += stride) {
        a[i] += C * b[idx[i]];
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}
void gather_maa(float *a, float *b, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting gather_maa min(" << min << "), max(" << max << "), stride(" << stride << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    // max scalar register
    int max_reg_id = get_new_reg<int>(max);
    // min scalar register
    int min_reg_id = get_new_reg<int>();
    // stride scalar register
    int stride_reg_id = get_new_reg<int>(stride);

    // allocate a tile of idx[i]
    int idx_tile = get_new_tile<int>();
    // allocate a tile of b[idx[i]]
    int b_tile = get_new_tile<float>();

    // real_i = i * stride + min
    // i = (real_i - min) / stride
    // min_i = 0
    // max_i = (max - min) / stride
    // Here we change a loop from min to max with stride to
    // a loop from 0 to i_max with stride = 1
    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        // i = min
        maa_const(curr_min, min_reg_id);
        // idx[i]
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        // b[idx[i]]
        maa_indirect_load<float>(b, idx_tile, b_tile);
        // get the SPD pointer to the b[idx[i]] tile
        float *b_p = get_cacheable_tile_pointer<float>(b_tile);
        // wait until the b tile is fetched to the SPD
        wait_ready(b_tile);
#pragma omp parallel for simd
        for (int i_offset = 0; i_offset < curr_tile_size; i_offset++) {
            int real_i = (i_base + i_offset) * stride + min;
            a[real_i] += C * b_p[i_offset];
        }
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void scatter(float *a, float *b, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting scatter min(" << min << "), max(" << max << "), stride(" << stride << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
#pragma omp for simd
    for (int i = min; i < max; i += stride) {
        a[idx[i]] = C * b[i];
        // std::cout << "a[idx[" << i << "](" << idx[i] << ")](" << a[idx[i]] << ") = " << C << " * b[" << i << "](" << b[i] << ")" << std::endl;
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}
void scatter_maa(float *a, float *b, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting scatter_maa min(" << min << "), max(" << max << "), stride(" << stride << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    // max scalar register
    int max_reg_id = get_new_reg<int>(max);
    // min scalar register
    int min_reg_id = get_new_reg<int>();
    // stride scalar register
    int stride_reg_id = get_new_reg<int>(stride);

    // allocate a tile of idx[i]
    int idx_tile = get_new_tile<int>();
    // allocate a tile of values that will be stored to a[idx[i]]
    int a_tile = get_new_tile<float>();

    // Here we change a loop from min to max with stride to
    // a loop from 0 to i_max with stride = 1
    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        // i = min
        maa_const(curr_min, min_reg_id);
        // idx[i]
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        // get the SPD address of a tile of values that will be stored to a[idx[i]]
        float *a_p = get_cacheable_tile_pointer<float>(a_tile);
#pragma omp parallel for simd
        for (int i_offset = 0; i_offset < curr_tile_size; i_offset++) {
            int real_i = (i_base + i_offset) * stride + min;
            a_p[i_offset] = C * b[real_i];
        }
        // store a tile of values from a SPD to a[idx[i]]
        maa_indirect_store<float>(a, idx_tile, a_tile);
        // wait until the a SPD tile is stored to the memory
        wait_ready(a_tile);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void rmw(float *a, float *b, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting rmw min(" << min << "), max(" << max << "), stride(" << stride << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
// TODO: parallelize
#pragma omp for simd
    for (int i = min; i < max; i += stride) {
        a[idx[i]] += C * b[i];
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}
void rmw_maa(float *a, float *b, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting rmw_maa min(" << min << "), max(" << max << "), stride(" << stride << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    // max scalar register
    int max_reg_id = get_new_reg<int>(max);
    // min scalar register
    int min_reg_id = get_new_reg<int>();
    // stride scalar register
    int stride_reg_id = get_new_reg<int>(stride);

    // allocate a tile of idx[i]
    int idx_tile = get_new_tile<int>();
    // allocate a tile of values that will be read-modified-written to a[idx[i]]
    int a_tile = get_new_tile<float>();

    // Here we change a loop from min to max with stride to
    // a loop from 0 to i_max with stride = 1
    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        // i = min
        maa_const(curr_min, min_reg_id);
        // idx[i]
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        // get the SPD address of a tile of values that will be read-modified-written to a[idx[i]]
        float *a_p = get_cacheable_tile_pointer<float>(a_tile);
#pragma omp parallel for simd
        for (int i_offset = 0; i_offset < curr_tile_size; i_offset++) {
            int real_i = (i_base + i_offset) * stride + min;
            a_p[i_offset] = C * b[real_i];
        }
        // Read-modify-write a tile of a[idx[i]] with the values in the a SPD tile
        maa_indirect_rmw<float>(a, idx_tile, a_tile, Operation_t::ADD_OP);
        // wait until the a SPD tile is read-modified-written to the memory
        wait_ready(a_tile);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_scatter(float *a, float *b, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting gather_scatter min(" << min << "), max(" << max << "), stride(" << stride << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
#pragma omp for simd
    for (int i = min; i < max; i += stride) {
        a[idx[i]] = C * b[idx[i]];
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}
void gather_scatter_maa(float *a, float *b, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting gather_scatter_maa min(" << min << "), max(" << max << "), stride(" << stride << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    // max scalar register
    int max_reg_id = get_new_reg<int>(max);
    // min scalar register
    int min_reg_id = get_new_reg<int>();
    // stride scalar register
    int stride_reg_id = get_new_reg<int>(stride);

    // allocate a tile of idx[i]
    int idx_tile = get_new_tile<int>();
    // allocate a tile of b[idx[i]]
    int b_tile = get_new_tile<float>();
    // allocate a tile of values that will be stored to a[idx[i]]
    int a_tile = get_new_tile<float>();

    // Here we change a loop from min to max with stride to
    // a loop from 0 to i_max with stride = 1
    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        // i = min
        maa_const(curr_min, min_reg_id);
        // idx[i]
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        // b[idx[i]]
        maa_indirect_load<float>(b, idx_tile, b_tile);
        // get the SPD pointer to the b[idx[i]] tile
        float *b_p = get_cacheable_tile_pointer<float>(b_tile);
        // get the SPD address of a tile of values that will be stored to a[idx[i]]
        float *a_p = get_cacheable_tile_pointer<float>(a_tile);
        // wait until the b tile is fetched to the SPD
        wait_ready(b_tile);
#pragma omp parallel for simd
        for (int i_offset = 0; i_offset < curr_tile_size; i_offset++) {
            int real_i = (i_base + i_offset) * stride + min;
            a_p[i_offset] = C * b_p[i_offset];
        }
        // store a tile of values from a SPD to a[idx[i]]
        maa_indirect_store<float>(a, idx_tile, a_tile);
        // wait until the a SPD tile is stored to the memory
        wait_ready(a_tile);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw(float *a, float *b, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting gather_rmw min(" << min << "), max(" << max << "), stride(" << stride << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
// TODO: parallelize
#pragma omp for simd
    for (int i = min; i < max; i += stride) {
        a[idx[i]] += C * b[idx[i]];
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}
void gather_rmw_maa(float *a, float *b, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting maa_gather_rmw min(" << min << "), max(" << max << "), stride(" << stride << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    // max scalar register
    int max_reg_id = get_new_reg<int>(max);
    // min scalar register
    int min_reg_id = get_new_reg<int>();
    // stride scalar register
    int stride_reg_id = get_new_reg<int>(stride);

    // allocate a tile of idx[i]
    int idx_tile = get_new_tile<int>();
    // allocate a tile of b[idx[i]]
    int b_tile = get_new_tile<float>();
    // allocate a tile of values that will be read-modified-written to a[idx[i]]
    int a_tile = get_new_tile<float>();

    // Here we change a loop from min to max with stride to
    // a loop from 0 to i_max with stride = 1
    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        // i = min
        maa_const(curr_min, min_reg_id);
        // idx[i]
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        // b[idx[i]]
        maa_indirect_load<float>(b, idx_tile, b_tile);
        // get the SPD pointer to the b[idx[i]] tile
        float *b_p = get_cacheable_tile_pointer<float>(b_tile);
        // get the SPD address of a tile of values that will be read-modified-written to a[idx[i]]
        float *a_p = get_cacheable_tile_pointer<float>(a_tile);
        // wait until the b[idx[i]] tile is fetched to the SPD
        wait_ready(b_tile);
#pragma omp parallel for simd
        for (int i_offset = 0; i_offset < curr_tile_size; i_offset++) {
            int real_i = (i_base + i_offset) * stride + min;
            a_p[i_offset] = C * b_p[i_offset];
        }
        // Read-modify-write a tile of a[idx[i]] with the values in the a SPD tile
        maa_indirect_rmw<float>(a, idx_tile, a_tile, Operation_t::ADD_OP);
        // wait until the a SPD tile is read-modified-written to the memory
        wait_ready(a_tile);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_cond(float *a, float *b, int *idx, int min, int max, int stride, const float mul_const, float *cond, const float cond_const) {
    std::cout << "starting gather_rmw_cond min(" << min << "), max(" << max << "), stride(" << stride << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
// TODO: parallelize
#pragma omp for simd
    for (int i = min; i < max; i += stride) {
        if (cond_const < cond[i]) {
            a[idx[i]] += mul_const * b[idx[i]];
        }
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}
void gather_rmw_cond_maa(float *a, float *b, int *idx, int min, int max, int stride, const float mul_const, float *cond, const float cond_const) {
    std::cout << "starting gather_rmw_cond_maa min(" << min << "), max(" << max << "), stride(" << stride << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    // max scalar register
    int max_reg_id = get_new_reg<int>(max);
    // min scalar register
    int min_reg_id = get_new_reg<int>();
    // stride scalar register
    int stride_reg_id = get_new_reg<int>(stride);
    // cond_const scalar register
    int cond_const_reg_id = get_new_reg<float>(cond_const);

    // allocate a tile of idx[i]
    int idx_tile = get_new_tile<int>();
    // allocate a tile of b[idx[i]]
    int b_tile = get_new_tile<float>();
    // allocate a tile of values that will be read-modified-written to a[idx[i]]
    int a_tile = get_new_tile<float>();
    // allocate a tile of cond[j] values
    int cond_tile = get_new_tile<float>();
    // allocate a tile of (cond_const < cond[i]) values
    int cond_res_tile = get_new_tile<uint32_t>();

    // Here we change a loop from min to max with stride to
    // a loop from 0 to i_max with stride = 1
    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        // i = min
        maa_const(curr_min, min_reg_id);
        // load a tile of cond[j] values to the cond SPD
        maa_stream_load<float>(cond, min_reg_id, max_reg_id, stride_reg_id, cond_tile);
        // if (cond_const < cond[i])
        maa_alu_scalar<float>(cond_tile, cond_const_reg_id, cond_res_tile, Operation_t::GT_OP);
        // load a tile of idx[i] values to the idx SPD if (cond_const < cond[i]) holds
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile, cond_res_tile);
        // load a tile of b[idx[i]] values to the b SPD if (cond_const < cond[i]) holds
        maa_indirect_load<float>(b, idx_tile, b_tile, cond_res_tile);
        // get the SPD pointer to the b[idx[i]] tile
        float *b_p = get_cacheable_tile_pointer<float>(b_tile);
        // get the SPD address of a tile of values that will be read-modified-written to a[idx[i]]
        float *a_p = get_cacheable_tile_pointer<float>(a_tile);
        // wait until the b tile is fetched to the SPD
        wait_ready(b_tile);
#pragma omp parallel for simd
        for (int i_offset = 0; i_offset < curr_tile_size; i_offset++) {
            // int real_i = (i_base + i_offset) * stride + min;
            a_p[i_offset] = mul_const * b_p[i_offset];
        }
        // Read-modify-write a tile of a[idx[i]] with the values in the a SPD tile
        // if (cond_const < cond[i]) holds
        maa_indirect_rmw<float>(a, idx_tile, a_tile, Operation_t::ADD_OP, cond_res_tile);
        // wait until the a SPD tile is read-modified-written to the memory
        wait_ready(a_tile);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_directrangeloop_cond(int *boundaries, float *a, float *b, int *idx, int min, int max, int stride, const float mul_const, float *cond, const float cond_const) {
    std::cout << "starting gather_rmw_directrangeloop_cond min(" << min << "), max(" << max << "), stride(" << stride << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
// TODO: parallelize
#pragma omp for simd
    for (int i = min; i < max; i += stride) {
        for (int j = boundaries[i]; j < boundaries[i + 1]; j++) {
            // std::cout << "[" << i << "][" << j << "]" << std::endl;
            if (cond_const < cond[j]) {
                // std::cout << "a[idx[" << j << "](" << idx[j] << ")](" << a[idx[j]] << ") += " << mul_const << " * b[idx[" << j << "](" << idx[j] << ")](" << b[idx[j]] << ")" << std::endl;
                a[idx[j]] += mul_const * b[idx[j]];
            }
        }
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}
void gather_rmw_directrangeloop_cond_maa(int *boundaries, float *a, float *b, int *idx, int min, int max, int stride, const float mul_const, float *cond, const float cond_const) {
    std::cout << "starting gather_rmw_directrangeloop_cond_maa min(" << min << "), max(" << max << "), stride(" << stride << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    // 1 scalar register
    int one_reg_id = get_new_reg<int>(1);
    // max scalar register
    int max_reg_id = get_new_reg<int>(max);
    // min scalar register
    int min_reg_id = get_new_reg<int>();
    // stride scalar register
    int stride_reg_id = get_new_reg<int>(stride);
    // cond_const scalar register
    int cond_const_reg_id = get_new_reg<float>(cond_const);
    // the last i and j values that the range loop has generated
    // used as the context for the next tile of j iterations
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    // allocate a tile of idx[j] values
    int idx_tile = get_new_tile<int>();
    // allocate a tile of b[idx[j]] values
    int b_tile = get_new_tile<float>();
    // allocate a tile of values that will be read-modified-written to a[idx[j]]
    int a_tile = get_new_tile<float>();
    // allocate a tile of cond[j] values
    int cond_tile = get_new_tile<float>();
    // allocate a tile of boundaries[i] and boundaries[i + 1] values
    int boundaries0_tile = get_new_tile<int>();
    int boundaries1_tile = get_new_tile<int>();
    // allocate a tile of i and j values that the range loop generates
    int i_tile = get_new_tile<int>();
    int j_tile = get_new_tile<int>();
    // allocate a tile of (cond_const < cond[j]) values
    int cond_res_tile = get_new_tile<uint32_t>();

    // get the SPD pointer to the b[idx[j]] tile
    float *b_p = get_cacheable_tile_pointer<float>(b_tile);
    // get the SPD address of a tile of values that will be read-modified-written to a[idx[j]]
    float *a_p = get_cacheable_tile_pointer<float>(a_tile);
    // get the SPD address of the i and j tiles
    int *i_p = get_cacheable_tile_pointer<int>(i_tile);
    int *j_p = get_cacheable_tile_pointer<int>(j_tile);
    // get the SPD address of the boundaries[i] and boundaries[i + 1] tiles
    int *boundaries0_p = get_cacheable_tile_pointer<int>(boundaries0_tile);
    int *boundaries1_p = get_cacheable_tile_pointer<int>(boundaries1_tile);

    // Here we change a loop from min to max with stride to
    // a loop from 0 to i_max with stride = 1
    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tilei_size = i_max - i_base;
        curr_tilei_size = curr_tilei_size > TILE_SIZE ? TILE_SIZE : curr_tilei_size;
        int curr_min = i_base * stride + min;
        // i = min
        maa_const(curr_min, min_reg_id);
        // load a tile of boundaries[i] and boundaries[i + 1] values to boundaries0 and boundaries1 SPD
        maa_stream_load<int>(boundaries, min_reg_id, max_reg_id, stride_reg_id, boundaries0_tile);
        maa_stream_load<int>(&boundaries[1], min_reg_id, max_reg_id, stride_reg_id, boundaries1_tile);

        int curr_tilej_size = 0;
        // i = 0, j = -1
        maa_const<int>(0, last_i_reg_id);
        maa_const<int>(-1, last_j_reg_id);
        // In each iteration of this do-while loop, we generate a tile of i and j
        // values corresponding to the current tile of i values.
        do {
            // generate a tile of i and j values for the current inner loop iteration tile
            maa_range_loop<float>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile);
            // load a tile of cond[j] values to the cond SPD
            maa_indirect_load<float>(cond, j_tile, cond_tile);
            // if (cond_const < cond[j])
            maa_alu_scalar<float>(cond_tile, cond_const_reg_id, cond_res_tile, Operation_t::GT_OP);
            // load a tile of idx[j] values to the idx SPD if (cond_const < cond[j]) holds
            maa_indirect_load<int>(idx, j_tile, idx_tile, cond_res_tile);
            // load a tile of b[idx[j]] values to the b SPD if (cond_const < cond[j]) holds
            maa_indirect_load<float>(b, idx_tile, b_tile, cond_res_tile);
            // wait until the b[idx[j]] tile is fetched to the SPD
            wait_ready(b_tile);
            // getting the size of i and j tiles which are the number of current iterations
            curr_tilej_size = get_tile_size(j_tile);
#pragma omp parallel for simd
            for (int j = 0; j < curr_tilej_size; j++) {
                int i_offset = i_p[j];
                int real_i = (i_base + i_offset) * stride + min;
                int real_j = j_p[j];
                if (cond_const < cond[real_j]) {
                    // std::cout << "[" << real_i << "][" << real_j << "]: a_p[" << j << "] += " << mul_const << " * b_p[" << j << "](" << b_p[j] << ")" << std::endl;
                    a_p[j] = mul_const * b_p[j];
                }
            }
            // Read-modify-write a tile of a[idx[j]] with the values in the a SPD tile
            // if (cond_const < cond[j]) holds
            maa_indirect_rmw<float>(a, idx_tile, a_tile, Operation_t::ADD_OP, cond_res_tile);
            // wait until the a SPD tile is read-modified-written to the memory
            wait_ready(a_tile);
        } while (curr_tilej_size > 0);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_indirectrangeloop_cond(int *nodes, int *boundaries, float *a, float *b, int *idx, int min, int max, int stride, const float mul_const, float *cond, const float cond_const) {
    std::cout << "starting gather_rmw_indirectrangeloop_cond min(" << min << "), max(" << max << "), stride(" << stride << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
// TODO: parallelize
#pragma omp for simd
    for (int i = min; i < max; i += stride) {
        for (int j = boundaries[nodes[i]]; j < boundaries[nodes[i] + 1]; j++) {
            // std::cout << "[" << i << "][" << j << "]" << std::endl;
            if (cond_const < cond[j]) {
                // std::cout << "a[idx[" << j << "](" << idx[j] << ")](" << a[idx[j]] << ") += " << mul_const << " * b[idx[" << j << "](" << idx[j] << ")](" << b[idx[j]] << ")" << std::endl;
                a[idx[j]] += mul_const * b[idx[j]];
            }
        }
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_indirectrangeloop_cond_maa(int *nodes, int *boundaries, float *a, float *b, int *idx, int min, int max, int stride, const float mul_const, float *cond, const float cond_const) {
    std::cout << "starting gather_rmw_indirectrangeloop_cond_maa min(" << min << "), max(" << max << "), stride(" << stride << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    // 1 scalar register
    int one_reg_id = get_new_reg<int>(1);
    // max scalar register
    int max_reg_id = get_new_reg<int>(max);
    // min scalar register
    int min_reg_id = get_new_reg<int>();
    // stride scalar register
    int stride_reg_id = get_new_reg<int>(stride);
    // cond_const scalar register
    int cond_const_reg_id = get_new_reg<float>(cond_const);
    // the last i and j values that the range loop has generated
    // used as the context for the next tile of j iterations
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    // allocate a tile of idx[j]
    int idx_tile = get_new_tile<int>();
    // allocate a tile of b[idx[j]]
    int b_tile = get_new_tile<float>();
    // allocate a tile of values that will be read-modified-written to a[idx[j]]
    int a_tile = get_new_tile<float>();
    // allocate a tile of cond[j] values
    int cond_tile = get_new_tile<float>();
    // allocate a tile of boundaries[nodes[i]] and boundaries[nodes[i] + 1] values
    int boundaries0_tile = get_new_tile<int>();
    int boundaries1_tile = get_new_tile<int>();
    // allocate a tile of i and j values that the range loop generates
    int i_tile = get_new_tile<int>();
    int j_tile = get_new_tile<int>();
    // allocate a tile of nodes[i] values
    int nodes_tile = get_new_tile<int>();
    // allocate a tile of (cond_const < cond[j]) values
    int cond_res_tile = get_new_tile<uint32_t>();

    // get the SPD pointer to the b[idx[j]] tile
    float *b_p = get_cacheable_tile_pointer<float>(b_tile);
    // get the SPD address of a tile of values that will be read-modified-written to a[idx[j]]
    float *a_p = get_cacheable_tile_pointer<float>(a_tile);
    // get the SPD address of the i and j tiles
    int *i_p = get_cacheable_tile_pointer<int>(i_tile);
    int *j_p = get_cacheable_tile_pointer<int>(j_tile);
    // get the SPD address of the boundaries[nodes[i]] and boundaries[nodes[i] + 1] tiles
    int *boundaries0_p = get_cacheable_tile_pointer<int>(boundaries0_tile);
    int *boundaries1_p = get_cacheable_tile_pointer<int>(boundaries1_tile);

    // Here we change a loop from min to max with stride to
    // a loop from 0 to i_max with stride = 1
    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        // i = min
        maa_const(curr_min, min_reg_id);
        // load a tile of nodes[i] values to the nodes SPD
        maa_stream_load<int>(nodes, min_reg_id, max_reg_id, stride_reg_id, nodes_tile);
        // load a tile of boundaries[nodes[i]] and boundaries[nodes[i] + 1] values to the boundaries0 and boundaries1 SPD
        maa_indirect_load<int>(boundaries, nodes_tile, boundaries0_tile);
        maa_indirect_load<int>(&boundaries[1], nodes_tile, boundaries1_tile);

        int curr_tilej_size = 0;
        // i = 0, j = -1
        maa_const<int>(0, last_i_reg_id);
        maa_const<int>(-1, last_j_reg_id);
        // In each iteration of this do-while loop, we generate a tile of i and j
        // values corresponding to the current tile of i values.
        do {
            // generate a tile of i and j values for the current inner loop iteration tile
            maa_range_loop<float>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile);
            // load a tile of cond[j] values to the cond SPD
            maa_indirect_load<float>(cond, j_tile, cond_tile);
            // if (cond_const < cond[j])
            maa_alu_scalar<float>(cond_tile, cond_const_reg_id, cond_res_tile, Operation_t::GT_OP);
            // load a tile of idx[j] values to the idx SPD if (cond_const < cond[j]) holds
            maa_indirect_load<int>(idx, j_tile, idx_tile, cond_res_tile);
            // load a tile of b[idx[j]] values to the b SPD if (cond_const < cond[j]) holds
            maa_indirect_load<float>(b, idx_tile, b_tile, cond_res_tile);
            // wait until the b[idx[j]] tile is fetched to the SPD
            wait_ready(b_tile);
            // getting the size of i and j tiles which are the number of current iterations
            curr_tilej_size = get_tile_size(j_tile);
#pragma omp parallel for simd
            for (int j = 0; j < curr_tilej_size; j++) {
                int i_offset = i_p[j];
                int real_i = (i_base + i_offset) * stride + min;
                int real_j = j_p[j];
                // std::cout << "[" << real_i << "][" << real_j << "]" << std::endl;
                if (cond_const < cond[real_j]) {
                    // std::cout << "a_p[" << j << "] += " << mul_const << " * b_p[" << j << "](" << b_p[j] << ")" << std::endl;
                    a_p[j] = mul_const * b_p[j];
                }
            }
            // Read-modify-write a tile of a[idx[j]] with the values in the a SPD tile
            // if (cond_const < cond[j]) holds
            maa_indirect_rmw<float>(a, idx_tile, a_tile, Operation_t::ADD_OP, cond_res_tile);
            // wait until the a[idx[j]] SPD tile is read-modified-written to the memory
            wait_ready(a_tile);
        } while (curr_tilej_size > 0);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_cond_indirectrangeloop_cond(int *nodes, int *boundaries, float *a, float *b, int *idx, int min, int max, int stride, const float mul_const, float *cond, const float cond_const) {
    std::cout << "starting gather_rmw_cond_indirectrangeloop_cond min(" << min << "), max(" << max << "), stride(" << stride << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
// TODO: parallelize
#pragma omp for simd
    for (int i = min; i < max; i += stride) {
        if (cond_const < cond[i]) {
            for (int j = boundaries[nodes[i]]; j < boundaries[nodes[i] + 1]; j++) {
                if (cond_const < cond[j]) {
                    a[idx[j]] += mul_const * b[idx[j]];
                }
            }
        }
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_cond_indirectrangeloop_cond_maa(int *nodes, int *boundaries, float *a, float *b, int *idx, int min, int max, int stride, const float mul_const, float *cond, const float cond_const) {
    std::cout << "starting gather_rmw_cond_indirectrangeloop_cond_maa min(" << min << "), max(" << max << "), stride(" << stride << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    // 1 scalar register
    int one_reg_id = get_new_reg<int>(1);
    // max scalar register
    int max_reg_id = get_new_reg<int>(max);
    // min scalar register
    int min_reg_id = get_new_reg<int>();
    // stride scalar register
    int stride_reg_id = get_new_reg<int>(stride);
    // cond_const scalar register
    int cond_const_reg_id = get_new_reg<float>(cond_const);
    // the last i and j values that the range loop has generated
    // used as the context for the next tile of j iterations
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    // allocate a tile of idx[j]
    int idx_tile = get_new_tile<int>();
    // allocate a tile of b[idx[j]]
    int b_tile = get_new_tile<float>();
    // allocate a tile of values that will be read-modified-written to a[idx[j]]
    int a_tile = get_new_tile<float>();
    // allocate a tile of cond[nodes[i]] and cond[nodes[j]] values
    int condi_tile = get_new_tile<float>();
    int condj_tile = get_new_tile<float>();
    // allocate a tile of boundaries[nodes[i]] and boundaries[nodes[i] + 1] values
    int boundaries0_tile = get_new_tile<int>();
    int boundaries1_tile = get_new_tile<int>();
    // allocate a tile of i and j values that the range loop generates
    int i_tile = get_new_tile<int>();
    int j_tile = get_new_tile<int>();
    // allocate a tile of nodes[i] values
    int nodes_tile = get_new_tile<int>();
    // allocate a tile of (cond_const < cond[i]) values
    int condi_res_tile = get_new_tile<uint32_t>();
    // allocate a tile of (cond_const < cond[j]) values
    int condj_res_tile = get_new_tile<uint32_t>();

    // get the SPD pointer to the b[idx[j]] tile
    float *b_p = get_cacheable_tile_pointer<float>(b_tile);
    // get the SPD address of a tile of values that will be
    // read-modified-written to a[idx[j]]
    float *a_p = get_cacheable_tile_pointer<float>(a_tile);
    // get the SPD address of the i and j tiles
    int *i_p = get_cacheable_tile_pointer<int>(i_tile);
    int *j_p = get_cacheable_tile_pointer<int>(j_tile);
    // get the SPD address of the boundaries[nodes[i]] and boundaries[nodes[i] + 1] tiles
    int *boundaries0_p = get_cacheable_tile_pointer<int>(boundaries0_tile);
    int *boundaries1_p = get_cacheable_tile_pointer<int>(boundaries1_tile);

    // Here we change a loop from min to max with stride to
    // a loop from 0 to i_max with stride = 1
    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        // i = min
        maa_const(curr_min, min_reg_id);
        // load a tile of cond[i] values to the cond SPD
        maa_stream_load<float>(cond, min_reg_id, max_reg_id, stride_reg_id, condi_tile);
        // if (cond_const < cond[i])
        maa_alu_scalar<float>(condi_tile, cond_const_reg_id, condi_res_tile, Operation_t::GT_OP);
        // load a tile of nodes[i] values to the nodes SPD if (cond_const < cond[i]) holds
        maa_stream_load<int>(nodes, min_reg_id, max_reg_id, stride_reg_id, nodes_tile, condi_res_tile);
        // load a tile of boundaries[nodes[i]] and boundaries[nodes[i] + 1] values
        // to the boundaries0 and boundaries1 SPD if (cond_const < cond[i]) holds
        maa_indirect_load<int>(boundaries, nodes_tile, boundaries0_tile, condi_res_tile);
        maa_indirect_load<int>(&boundaries[1], nodes_tile, boundaries1_tile, condi_res_tile);

        int curr_tilej_size = 0;
        // i = 0, j = -1
        maa_const<int>(0, last_i_reg_id);
        maa_const<int>(-1, last_j_reg_id);
        // In each iteration of this do-while loop, we generate a tile of i and j
        // values corresponding to the current tile of i values.
        do {
            // generate a tile of i and j values for the current inner loop
            // iteration tile if (cond_const < cond[i]) holds
            maa_range_loop<float>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile, condi_res_tile);
            // load a tile of cond[j] values to the cond SPD
            maa_indirect_load<float>(cond, j_tile, condj_tile);
            // if (cond_const < cond[j])
            maa_alu_scalar<float>(condj_tile, cond_const_reg_id, condj_res_tile, Operation_t::GT_OP);
            // load a tile of idx[j] values to the idx SPD if (cond_const < cond[j]) holds
            maa_indirect_load<int>(idx, j_tile, idx_tile, condj_res_tile);
            // load a tile of b[idx[j]] values to the b SPD if (cond_const < cond[j]) holds
            maa_indirect_load<float>(b, idx_tile, b_tile, condj_res_tile);
            // wait until the b[idx[j]] tile is fetched to the SPD
            wait_ready(b_tile);
            // getting the size of i and j tiles which are the number of current iterations
            curr_tilej_size = get_tile_size(j_tile);
#pragma omp parallel for simd
            for (int j = 0; j < curr_tilej_size; j++) {
                int i_offset = i_p[j];
                int real_i = (i_base + i_offset) * stride + min;
                int real_j = j_p[j];
                if (cond_const < cond[real_j]) {
                    a_p[j] = mul_const * b_p[j];
                }
            }
            // Read-modify-write a tile of a[idx[j]] with the values in the a SPD tile
            maa_indirect_rmw<float>(a, idx_tile, a_tile, Operation_t::ADD_OP, condj_res_tile);
            // wait until the a[idx[j]] SPD tile is read-modified-written to the memory
            wait_ready(a_tile);
        } while (curr_tilej_size > 0);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_indirectcond_indirectrangeloop_indirectcond(int *nodes, int *boundaries, float *a, float *b, int *idx, int min, int max, int stride, const float mul_const, float *cond, const float cond_const) {
    std::cout << "starting gather_rmw_indirectcond_indirectrangeloop_indirectcond min(" << min << "), max(" << max << "), stride(" << stride << ")" << std::endl;
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
// TODO: parallelize
#pragma omp for simd
    for (int i = min; i < max; i += stride) {
        if (cond_const < cond[nodes[i]]) {
            for (int j = boundaries[nodes[i]]; j < boundaries[nodes[i] + 1]; j++) {
                if (cond_const < cond[nodes[j]]) {
                    a[idx[j]] += mul_const * b[idx[j]];
                }
            }
        }
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void gather_rmw_indirectcond_indirectrangeloop_indirectcond_maa(int *nodes, int *boundaries, float *a, float *b, int *idx, int min, int max, int stride, const float mul_const, float *cond, const float cond_const) {
    std::cout << "starting gather_rmw_indirectcond_indirectrangeloop_indirectcond_maa min(" << min << "), max(" << max << "), stride(" << stride << "), tile(" << TILE_SIZE << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    // 1 scalar register
    int one_reg_id = get_new_reg<int>(1);
    // max scalar register
    int max_reg_id = get_new_reg<int>(max);
    // min scalar register
    int min_reg_id = get_new_reg<int>();
    // stride scalar register
    int stride_reg_id = get_new_reg<int>(stride);
    // cond_const scalar register
    int cond_const_reg_id = get_new_reg<float>(cond_const);
    // the last i and j values that the range loop has generated
    // used as the context for the next tile of j iterations
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    // allocate a tile of idx[j]
    int idx_tile = get_new_tile<int>();
    // allocate a tile of b[idx[j]]
    int b_tile = get_new_tile<float>();
    // allocate a tile of values that will be read-modified-written to a[idx[j]]
    int a_tile = get_new_tile<float>();
    // allocate a tile of cond[nodes[i]] and cond[nodes[j]] values
    int condi_tile = get_new_tile<float>();
    int condj_tile = get_new_tile<float>();
    // allocate a tile of boundaries[nodes[i]] and boundaries[nodes[i] + 1] values
    int boundaries0_tile = get_new_tile<int>();
    int boundaries1_tile = get_new_tile<int>();
    // allocate a tile of i and j values that the range loop generates
    int i_tile = get_new_tile<int>();
    int j_tile = get_new_tile<int>();
    // allocate a tile of nodes[i] and nodes[j] values
    int nodesi_tile = get_new_tile<int>();
    int nodesj_tile = get_new_tile<int>();
    // allocate a tile of (cond_const < cond[nodes[i]]) values
    int condi_res_tile = get_new_tile<uint32_t>();
    // allocate a tile of (cond_const < cond[nodes[j]]) values
    int condj_res_tile = get_new_tile<uint32_t>();

    // get the SPD pointer to the b[idx[j]] tile
    float *b_p = get_cacheable_tile_pointer<float>(b_tile);
    // get the SPD address of a tile of values that will be read-modified-written to a[idx[j]]
    float *a_p = get_cacheable_tile_pointer<float>(a_tile);
    // get the SPD address of the i and j tiles
    int *i_p = get_cacheable_tile_pointer<int>(i_tile);
    int *j_p = get_cacheable_tile_pointer<int>(j_tile);
    // get the SPD address of the boundaries[nodes[i]] and boundaries[nodes[i] + 1] tiles
    int *boundaries0_p = get_cacheable_tile_pointer<int>(boundaries0_tile);
    int *boundaries1_p = get_cacheable_tile_pointer<int>(boundaries1_tile);
    // get the SPD address of the cond[nodes[i]] and cond[nodes[j]] tiles
    float *condi_p = get_cacheable_tile_pointer<float>(condi_tile);
    float *condj_p = get_cacheable_tile_pointer<float>(condj_tile);

    // Here we change a loop from min to max with stride to
    // a loop from 0 to i_max with stride = 1
    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        // i = min
        maa_const(curr_min, min_reg_id);
        // load a tile of nodes[i] values to the nodesi SPD
        maa_stream_load<int>(nodes, min_reg_id, max_reg_id, stride_reg_id, nodesi_tile);
        // load a tile of cond[nodes[i]] values to the condi SPD
        maa_indirect_load<float>(cond, nodesi_tile, condi_tile);
        // if (cond_const < cond[i])
        maa_alu_scalar<float>(condi_tile, cond_const_reg_id, condi_res_tile, Operation_t::GT_OP);
        // load a tile of boundaries[nodes[i]] and boundaries[nodes[i] + 1] values
        // to the boundaries0 and boundaries1 SPD if (cond_const < cond[nodes[i]]) holds
        maa_indirect_load<int>(boundaries, nodesi_tile, boundaries0_tile, condi_res_tile);
        maa_indirect_load<int>(&boundaries[1], nodesi_tile, boundaries1_tile, condi_res_tile);

        int curr_tilej_size = 0;
        // i = 0, j = -1
        maa_const<int>(0, last_i_reg_id);
        maa_const<int>(-1, last_j_reg_id);
        // In each iteration of this do-while loop, we generate a tile of i and j
        // values corresponding to the current tile of i values.
        do {
            // generate a tile of i and j values for the current inner loop
            // iteration tile if (cond_const < cond[nodes[i]]) holds
            maa_range_loop<float>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile, condi_res_tile);
            // load a tile of nodes[j] values to the nodesj SPD
            maa_indirect_load<int>(nodes, j_tile, nodesj_tile);
            // load a tile of cond[nodes[j]] values to the condj SPD
            maa_indirect_load<float>(cond, nodesj_tile, condj_tile);
            // if (cond_const < cond[j])
            maa_alu_scalar<float>(condj_tile, cond_const_reg_id, condj_res_tile, Operation_t::GT_OP);
            // load a tile of idx[j] values to the idxj SPD if (cond_const < cond[nodes[j]]) holds
            maa_indirect_load<int>(idx, j_tile, idx_tile, condj_res_tile);
            // load a tile of b[idx[j]] values to the b SPD if (cond_const < cond[nodes[j]]) holds
            maa_indirect_load<float>(b, idx_tile, b_tile, condj_res_tile);
            // wait until the b[idx[j]] tile is fetched to the SPD
            wait_ready(b_tile);
            // getting the size of i and j tiles which are the number of current inner loop iterations
            curr_tilej_size = get_tile_size(j_tile);
#pragma omp parallel for simd
            for (int j = 0; j < curr_tilej_size; j++) {
                int i_offset = i_p[j];
                // Here we can calculate the real i and j values in the
                // legacy C code as follows:
                int real_i = (i_base + i_offset) * stride + min;
                int real_j = j_p[j];
                if (cond_const < condj_p[j]) {
                    a_p[j] = mul_const * b_p[j];
                }
            }
            // Read-modify-write a tile of a[idx[j]] with the values in the a SPD tile
            maa_indirect_rmw<float>(a, idx_tile, a_tile, Operation_t::ADD_OP, condj_res_tile);
            // wait until the a[idx[j]] SPD tile is read-modified-written to the memory
            wait_ready(a_tile);
        } while (curr_tilej_size > 0);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void flush_cache() {
    std::cout << "flushing the cache" << std::endl;
    // Global variables.
    const size_t bigger_than_cachesize = 3 * 1024 * 1024;
    long *p = new long[bigger_than_cachesize];
// When you want to "flush" cache.
#pragma omp parallel for simd
    for (int i = 0; i < 1024; i++) {
        std::cout << "iter" << i << std::endl;
        for (int j = 0; j < 3 * 1024; j++) {
            p[i * 3 * 1024 + j] = rand();
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
        return 1;
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
        return 1;
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
    for (long i = 0; i < (long)n * S; i += 1) {
        a1[i] = a2[i] = rand() % 1024;
        cond[i] = rand() % 1024;
        b[i] = rand() % 1024;
        int tmp = rand() % (2 * d) % (n * S);
        idx[i] = (i - tmp) < 0 ? tmp : (i - tmp);
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
    int stride = 1;      // rand() % 5 + 1;

#ifdef GEM5
    std::cout << "Checkpointing started" << std::endl;
    m5_checkpoint(0, 0);
    std::cout << "Checkpointing ended" << std::endl;
#endif

    std::cout << "initializing done, testing..." << std::endl;

    if (maa) {
        alloc_MAA();
    }

    if (kernel == "gather" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        stride = 1;      // rand() % 5 + 1;
        if (maa) {
            gather_maa(a2, b, idx, min, max, stride, C);
        }
        if (base) {
            gather(a1, b, idx, min, max, stride, C);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i += 1) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- Gather: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    return 1;
                }
            }
            cout << "gather correct" << std::endl;
        }
    }

    if (kernel == "scatter" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        stride = 1;      // rand() % 5 + 1;
        if (maa) {
            scatter_maa(a2, b, idx, min, max, stride, C);
        }
        if (base) {
            scatter(a1, b, idx, min, max, stride, C);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i += 1) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- Scatter: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    return 1;
                }
            }
            cout << "scatter correct" << std::endl;
        }
    }

    if (kernel == "rmw" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        stride = 1;      // rand() % 5 + 1;
        if (maa) {
            rmw_maa(a2, b, idx, min, max, stride, C);
        }
        if (base) {
            rmw(a1, b, idx, min, max, stride, C);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i += 1) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- rmw: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    return 1;
                }
            }
            cout << "rmw correct" << std::endl;
        }
    }

    if (kernel == "gather_scatter" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        stride = 1;      // rand() % 5 + 1;
        if (maa) {
            gather_scatter_maa(a2, b, idx, min, max, stride, C);
        }
        if (base) {
            gather_scatter(a1, b, idx, min, max, stride, C);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i += 1) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- gather_scatter: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    return 1;
                }
            }
            cout << "gather_scatter correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        stride = 1;      // rand() % 5 + 1;
        if (maa) {
            gather_rmw_maa(a2, b, idx, min, max, stride, C);
        }
        if (base) {
            gather_rmw(a1, b, idx, min, max, stride, C);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i += 1) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- gather_rmw: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    return 1;
                }
            }
            cout << "gather_rmw correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw_cond" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        stride = 1;      // rand() % 5 + 1;
        if (maa) {
            gather_rmw_cond_maa(a2, b, idx, min, max, stride, C, cond, 128);
        }
        if (base) {
            gather_rmw_cond(a1, b, idx, min, max, stride, C, cond, 128);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i += 1) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- gather_rmw_cond: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    return 1;
                }
            }
            cout << "gather_rmw_cond correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw_directrangeloop_cond" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        stride = 1;      // rand() % 5 + 1;
        if (maa) {
            gather_rmw_directrangeloop_cond_maa(boundaries, a2, b, idx, min, max, stride, C, cond, 128);
        }
        if (base) {
            gather_rmw_directrangeloop_cond(boundaries, a1, b, idx, min, max, stride, C, cond, 128);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i += 1) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- gather_rmw_directrangeloop_cond: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    return 1;
                }
            }
            cout << "gather_rmw_directrangeloop_cond correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw_indirectrangeloop_cond" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        stride = 1;      // rand() % 5 + 1;
        if (maa) {
            gather_rmw_indirectrangeloop_cond_maa(nodes, boundaries, a2, b, idx, min, max, stride, C, cond, 128);
        }
        if (base) {
            gather_rmw_indirectrangeloop_cond(nodes, boundaries, a1, b, idx, min, max, stride, C, cond, 128);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i += 1) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- gather_rmw_indirectrangeloop_cond: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    return 1;
                }
            }
            cout << "gather_rmw_indirectrangeloop_cond correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw_cond_indirectrangeloop_cond" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        stride = 1;      // rand() % 5 + 1;
        if (maa) {
            gather_rmw_cond_indirectrangeloop_cond_maa(nodes, boundaries, a2, b, idx, min, max, stride, C, cond, 128);
        }
        if (base) {
            gather_rmw_cond_indirectrangeloop_cond(nodes, boundaries, a1, b, idx, min, max, stride, C, cond, 128);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i += 1) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- gather_rmw_cond_indirectrangeloop_cond: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    return 1;
                }
            }
            cout << "gather_rmw_cond_indirectrangeloop_cond correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw_indirectcond_indirectrangeloop_indirectcond" || kernel == "all") {
        min = 0;         // rand() % (num_nodes / 8);
        max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
        stride = 1;      // rand() % 5 + 1;
        if (maa) {
            gather_rmw_indirectcond_indirectrangeloop_indirectcond_maa(nodes, boundaries, a2, b, idx, min, max, stride, C, cond, 128);
        }
        if (base) {
            gather_rmw_indirectcond_indirectrangeloop_indirectcond(nodes, boundaries, a1, b, idx, min, max, stride, C, cond, 128);
        }
        if (cmp) {
            for (int i = 0; i < (long)n * S; i += 1) {
                if (abs(a1[i] - a2[i]) >= DELTA) {
                    cout << "Error -- gather_rmw_indirectcond_indirectrangeloop_indirectcond: " << i << " " << a1[i] << " " << a2[i] << endl;
                    free(nodes);
                    free(boundaries);
                    free(a1);
                    free(a2);
                    free(cond);
                    free(b);
                    free(idx);
                    return 1;
                }
            }
            cout << "gather_rmw_indirectcond_indirectrangeloop_indirectcond correct" << std::endl;
        }
    }
#ifdef COMPILER_TEST
    cout << "LOG: Start Compiler Tests" << endl;
    min = 0;         // rand() % (num_nodes / 8);
    max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    gather_compiler(a1, b, idx, min, max, C);
    gather(a2, b, idx, min, max, 1, C);
    for (int i = 0; i < (long)n * S; i += 1) {
        if (abs(a1[i] - a2[i]) >= DELTA) {
            cout << "Error -- Gather Compiler: " << i << " " << a1[i] << " " << a2[i] << endl;
            free(nodes);
            free(boundaries);
            free(a1);
            free(a2);
            free(cond);
            free(b);
            free(idx);
            return 1;
        }
    }
    cout << "gather_compiler correct" << std::endl;

    min = 0;         // rand() % (num_nodes / 8);
    max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    scatter_compiler(a1, b, idx, min, max, C);
    scatter(a2, b, idx, min, max, 1, C);
    for (int i = 0; i < (long)n * S; i += 1) {
        if (abs(a1[i] - a2[i]) >= DELTA) {
            cout << "Error -- Scatter Compiler: " << i << " " << a1[i] << " " << a2[i] << endl;
            free(nodes);
            free(boundaries);
            free(a1);
            free(a2);
            free(cond);
            free(b);
            free(idx);
            return 1;
        }
    }
    cout << "scatter_compiler correct" << std::endl;

    min = 0;         // rand() % (num_nodes / 8);
    max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    rmw_compiler(a1, b, idx, min, max, C);
    rmw(a2, b, idx, min, max, 1, C);
    for (int i = 0; i < (long)n * S; i += 1) {
        if (abs(a1[i] - a2[i]) >= DELTA) {
            cout << "Error -- rmw Compiler: " << i << " " << a1[i] << " " << a2[i] << endl;
            free(nodes);
            free(boundaries);
            free(a1);
            free(a2);
            free(cond);
            free(b);
            free(idx);
            return 1;
        }
    }
    cout << "rmw_compiler correct" << std::endl;

    min = 0;         // rand() % (num_nodes / 8);
    max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    gather_scatter_compiler(a1, b, idx, min, max, C);
    gather_scatter(a2, b, idx, min, max, 1, C);
    for (int i = 0; i < (long)n * S; i += 1) {
        if (abs(a1[i] - a2[i]) >= DELTA) {
            cout << "Error -- gather_scatter Compiler: " << i << " " << a1[i] << " " << a2[i] << endl;
            free(nodes);
            free(boundaries);
            free(a1);
            free(a2);
            free(cond);
            free(b);
            free(idx);
            return 1;
        }
    }
    cout << "gather_scatter_compiler correct" << std::endl;

    min = 0;         // rand() % (num_nodes / 8);
    max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    gather_rmw_compiler(a1, b, idx, min, max, C);
    gather_rmw(a2, b, idx, min, max, 1, C);
    for (int i = 0; i < (long)n * S; i += 1) {
        if (abs(a1[i] - a2[i]) >= DELTA) {
            cout << "Error -- gather_rmw Compiler: " << i << " " << a1[i] << " " << a2[i] << endl;
            free(nodes);
            free(boundaries);
            free(a1);
            free(a2);
            free(cond);
            free(b);
            free(idx);
            return 1;
        }
    }
    cout << "gather_rmw_compiler correct" << std::endl;

    min = 0;         // rand() % (num_nodes / 8);
    max = num_nodes; // num_nodes - rand() % (num_nodes / 8);
    gather_rmw_cond_compiler(a1, b, idx, min, max, C, cond, 128);
    gather_rmw_cond(a2, b, idx, min, max, 1, C, cond, 128);
    for (int i = 0; i < (long)n * S; i += 1) {
        if (abs(a1[i] - a2[i]) >= DELTA) {
            cout << "Error -- gather_rmw_cond Compiler: " << i << " " << a1[i] << " " << a2[i] << endl;
            free(nodes);
            free(boundaries);
            free(a1);
            free(a2);
            free(cond);
            free(b);
            free(idx);
            return 1;
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