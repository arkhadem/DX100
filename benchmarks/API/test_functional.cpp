
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

template <class T>
void gather(T *__restrict__ a, T *__restrict__ b, int *__restrict__ idx, int min, int max, int stride, const T C) {
    std::cout << "starting gather min(" << min << "), max(" << max << ")" << std::endl;
#pragma omp parallel
    {
#pragma omp for simd aligned(a, b : 16) simdlen(4)
        for (int i = min; i < max; i += stride) {
            a[i] = C * b[idx[i]];
        }
    }
}

template <class T>
void gather_maa(T *__restrict__ a, T *__restrict__ b, int *__restrict__ idx, int min, int max, int stride, const T C) {
    std::cout << "starting gather_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;
    init_MAA();
    int max_reg_id, min_reg_id, stride_reg_id, idx_tile, b_tile, a_tile;
    T *b_p;
    T *a_s;
#pragma omp parallel
    {
#pragma omp single
        {
            max_reg_id = get_new_reg<int>(max);
            min_reg_id = get_new_reg<int>();
            stride_reg_id = get_new_reg<int>(stride);
            idx_tile = get_new_tile<int>();
            b_tile = get_new_tile<T>();
            b_p = get_cacheable_tile_pointer<T>(b_tile);
        }
        int i_max = (max - min - 1) / stride + 1;
        for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
            int curr_tile_size = i_max - i_base;
            curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
            int curr_min = i_base * stride + min;
#pragma omp single
            {
                maa_const(curr_min, min_reg_id);
                maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
                maa_indirect_load<T>(b, idx_tile, b_tile);
                a_s = &(a[min + i_base * stride]);
                wait_ready(b_tile);
            }
#pragma omp for simd aligned(a_s, b_p : 16) simdlen(4)
            for (int i_offset = 0; i_offset < curr_tile_size; i_offset++) {
                a_s[i_offset * stride] = C * b_p[i_offset];
            }
        }
    }
}

template <class T>
void scatter(T *a, T *b, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting scatter min(" << min << "), max(" << max << ")" << std::endl;
    for (int i = min; i < max; i += stride) {
        a[idx[i]] = C * b[i];
    }
}
template <class T>
void scatter_maa(T *a, T *b, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting scatter_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

    init_MAA();

    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(stride);
    int C_reg_id = get_new_reg<T>(C);

    int idx_tile = get_new_tile<int>();
    int a_tile = get_new_tile<T>();
    int b_tile = get_new_tile<T>();

    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        wait_ready(idx_tile);
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        maa_stream_load<T>(b, min_reg_id, max_reg_id, stride_reg_id, b_tile);
        maa_alu_scalar<T>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP);
        maa_indirect_store_vector<T>(a, idx_tile, a_tile);
    }
    wait_ready(a_tile);
}

template <class T>
void rmw(T *a, T *b, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting rmw min(" << min << "), max(" << max << ")" << std::endl;
    for (int i = min; i < max; i += stride) {
        a[idx[i]] += C * b[i];
    }
}
template <class T>
void rmw_maa(T *a, T *b, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting rmw_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

    init_MAA();

    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(stride);
    int C_reg_id = get_new_reg<T>(C);

    int idx_tile = get_new_tile<int>();
    int a_tile = get_new_tile<T>();
    int b_tile = get_new_tile<T>();

    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        wait_ready(idx_tile);
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        maa_stream_load<T>(b, min_reg_id, max_reg_id, stride_reg_id, b_tile);
        maa_alu_scalar<T>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP);
        maa_indirect_rmw_vector<T>(a, idx_tile, a_tile, Operation_t::ADD_OP);
    }
    wait_ready(a_tile);
}

template <class T>
void gather_scatter(T *a, T *b, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting gather_scatter min(" << min << "), max(" << max << ")" << std::endl;
    for (int i = min; i < max; i += stride) {
        a[idx[i]] = C * b[idx[i]];
    }
}
template <class T>
void gather_scatter_maa(T *a, T *b, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting gather_scatter_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

    init_MAA();

    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(stride);
    int C_reg_id = get_new_reg<T>(C);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<T>();
    int a_tile = get_new_tile<T>();

    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        wait_ready(idx_tile);
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        maa_indirect_load<T>(b, idx_tile, b_tile);
        maa_alu_scalar<T>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP);
        maa_indirect_store_vector<T>(a, idx_tile, a_tile);
    }
    wait_ready(a_tile);
}

template <class T>
void gather_rmw(T *a, T *b, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting gather_rmw min(" << min << "), max(" << max << ")" << std::endl;
    // TODO: parallelize
    for (int i = min; i < max; i += stride) {
        a[idx[i]] += C * b[idx[i]];
    }
}
template <class T>
void gather_rmw_maa(T *a, T *b, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting gather_rmw_maa min(" << min << "), max(" << max << ")" << std::endl;

    init_MAA();

    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(stride);
    int C_reg_id = get_new_reg<T>(C);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<T>();
    int a_tile = get_new_tile<T>();

    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        wait_ready(idx_tile);
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        maa_indirect_load<T>(b, idx_tile, b_tile);
        maa_alu_scalar<T>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP);
        maa_indirect_rmw_vector<T>(a, idx_tile, a_tile, Operation_t::ADD_OP);
    }
    wait_ready(a_tile);
}
template <class T>
void gather_rmw_dst(T *a, T *b, T *c, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting gather_rmw_dst min(" << min << "), max(" << max << ")" << std::endl;
    for (int i = min; i < max; i += stride) {
        c[i] = a[idx[i]];
        a[idx[i]] += C * b[idx[i]];
    }
}
template <class T>
void gather_rmw_dst_maa(T *a, T *b, T *c, int *idx, int min, int max, int stride, const int C) {
    std::cout << "starting gather_rmw_dst_maa min(" << min << "), max(" << max << ")" << std::endl;

    init_MAA();

    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(stride);
    int C_reg_id = get_new_reg<T>(C);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<T>();
    int a_tile = get_new_tile<T>();
    int c_tile = get_new_tile<T>();

    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        wait_ready(idx_tile);
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile);
        maa_indirect_load<T>(b, idx_tile, b_tile);
        maa_alu_scalar<T>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP);
        maa_indirect_rmw_vector<T>(a, idx_tile, a_tile, Operation_t::ADD_OP, -1, c_tile);
        maa_stream_store<T>(c, min_reg_id, max_reg_id, stride_reg_id, c_tile);
    }
    wait_ready(a_tile);
}

template <class T>
void gather_rmw_cond(T *a, T *b, int *idx, int min, int max, int stride, const T C, T *cond, const T cond_const) {
    std::cout << "starting gather_rmw_cond min(" << min << "), max(" << max << ")" << std::endl;
    // TODO: parallelize
    for (int i = min; i < max; i += stride) {
        if (cond_const < cond[i]) {
            a[idx[i]] += C * b[idx[i]];
        }
    }
}
template <class T>
void gather_rmw_cond_maa(T *a, T *b, int *idx, int min, int max, int stride, const T C, T *cond, const T cond_const) {
    std::cout << "starting gather_rmw_cond_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

    init_MAA();

    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(stride);
    int C_reg_id = get_new_reg<T>(C);

    int cond_const_reg_id = get_new_reg<T>(cond_const);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<T>();
    int a_tile = get_new_tile<T>();
    int cond_tile = get_new_tile<T>();
    int cond_res_tile = get_new_tile<uint32_t>();

    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        wait_ready(cond_tile);
        maa_const(curr_min, min_reg_id);
        maa_stream_load<T>(cond, min_reg_id, max_reg_id, stride_reg_id, cond_tile);
        maa_alu_scalar<T>(cond_tile, cond_const_reg_id, cond_res_tile, Operation_t::GT_OP);
        maa_stream_load<int>(idx, min_reg_id, max_reg_id, stride_reg_id, idx_tile, cond_res_tile);
        maa_indirect_load<T>(b, idx_tile, b_tile, cond_res_tile);
        maa_alu_scalar<T>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP, cond_res_tile);
        maa_indirect_rmw_vector<T>(a, idx_tile, a_tile, Operation_t::ADD_OP, cond_res_tile);
    }
    wait_ready(a_tile);
}

template <class T>
void gather_rmw_directrangeloop_cond(int *boundaries, T *a, T *b, int *idx, int min, int max, int stride, const T C, T *cond, const T cond_const) {
    std::cout << "starting gather_rmw_directrangeloop_cond min(" << min << "), max(" << max << ")" << std::endl;
    for (int i = min; i < max; i += stride) {
        for (int j = boundaries[i]; j < boundaries[i + 1]; j++) {
            if (cond_const < cond[j]) {
                a[idx[j]] += C * b[idx[j]];
            }
        }
    }
}
template <class T>
void gather_rmw_directrangeloop_cond_maa(int *boundaries, T *a, T *b, int *idx, int min, int max, int stride, const T C, T *cond, const T cond_const) {
    std::cout << "starting gather_rmw_directrangeloop_cond_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

    init_MAA();

    int one_reg_id = get_new_reg<int>(1);
    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(stride);
    int C_reg_id = get_new_reg<T>(C);

    int cond_const_reg_id = get_new_reg<T>(cond_const);
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<T>();
    int a_tile = get_new_tile<T>();
    int cond_tile = get_new_tile<T>();
    int boundaries0_tile = get_new_tile<int>();
    int boundaries1_tile = get_new_tile<int>();
    int i_tile = get_new_tile<int>();
    int j_tile = get_new_tile<int>();
    int cond_res_tile = get_new_tile<uint32_t>();

    T *b_p = get_cacheable_tile_pointer<T>(b_tile);
    T *a_p = get_cacheable_tile_pointer<T>(a_tile);
    uint32_t *cond_res_p = get_cacheable_tile_pointer<uint32_t>(cond_res_tile);

    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tilei_size = i_max - i_base;
        curr_tilei_size = curr_tilei_size > TILE_SIZE ? TILE_SIZE : curr_tilei_size;
        int curr_min = i_base * stride + min;
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(boundaries, min_reg_id, max_reg_id, stride_reg_id, boundaries0_tile);
        maa_stream_load<int>(&boundaries[1], min_reg_id, max_reg_id, stride_reg_id, boundaries1_tile);

        int curr_tilej_size = 0;
        maa_const<int>(0, last_i_reg_id);
        maa_const<int>(-1, last_j_reg_id);
        do {
            maa_range_loop<T>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile);
            maa_indirect_load<T>(cond, j_tile, cond_tile);
            maa_alu_scalar<T>(cond_tile, cond_const_reg_id, cond_res_tile, Operation_t::GT_OP);
            maa_indirect_load<int>(idx, j_tile, idx_tile, cond_res_tile);
            maa_indirect_load<T>(b, idx_tile, b_tile, cond_res_tile);
            maa_alu_scalar<T>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP, cond_res_tile);
            maa_indirect_rmw_vector<T>(a, idx_tile, a_tile, Operation_t::ADD_OP, cond_res_tile);
            wait_ready(j_tile);
            curr_tilej_size = get_tile_size(j_tile);
        } while (curr_tilej_size > 0);
        wait_ready(a_tile);
    }
}

template <class T>
void gather_rmw_indirectrangeloop_cond(int *nodes, int *boundaries, T *a, T *b, int *idx, int min, int max, int stride, const T C, T *cond, const T cond_const) {
    std::cout << "starting gather_rmw_indirectrangeloop_cond min(" << min << "), max(" << max << ")" << std::endl;
    // TODO: parallelize
    for (int i = min; i < max; i += stride) {
        for (int j = boundaries[nodes[i]]; j < boundaries[nodes[i] + 1]; j++) {
            if (cond_const < cond[j]) {
                a[idx[j]] += C * b[idx[j]];
            }
        }
    }
}

template <class T>
void gather_rmw_indirectrangeloop_cond_maa(int *nodes, int *boundaries, T *a, T *b, int *idx, int min, int max, int stride, const T C, T *cond, const T cond_const) {
    std::cout << "starting gather_rmw_indirectrangeloop_cond_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

    init_MAA();

    int one_reg_id = get_new_reg<int>(1);
    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(stride);
    int C_reg_id = get_new_reg<T>(C);

    int cond_const_reg_id = get_new_reg<T>(cond_const);
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<T>();
    int a_tile = get_new_tile<T>();
    int cond_tile = get_new_tile<T>();
    int boundaries0_tile = get_new_tile<int>();
    int boundaries1_tile = get_new_tile<int>();
    int i_tile = get_new_tile<int>();
    int j_tile = get_new_tile<int>();
    int nodes_tile = get_new_tile<int>();
    int cond_res_tile = get_new_tile<uint32_t>();

    T *b_p = get_cacheable_tile_pointer<T>(b_tile);
    T *a_p = get_cacheable_tile_pointer<T>(a_tile);
    int *i_p = get_cacheable_tile_pointer<int>(i_tile);
    int *j_p = get_cacheable_tile_pointer<int>(j_tile);
    uint32_t *cond_res_p = get_cacheable_tile_pointer<uint32_t>(cond_res_tile);

    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(nodes, min_reg_id, max_reg_id, stride_reg_id, nodes_tile);
        maa_indirect_load<int>(boundaries, nodes_tile, boundaries0_tile);
        maa_indirect_load<int>(&boundaries[1], nodes_tile, boundaries1_tile);

        int curr_tilej_size = 0;
        maa_const<int>(0, last_i_reg_id);
        maa_const<int>(-1, last_j_reg_id);
        do {
            maa_range_loop<T>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile);
            maa_indirect_load<T>(cond, j_tile, cond_tile);
            maa_alu_scalar<T>(cond_tile, cond_const_reg_id, cond_res_tile, Operation_t::GT_OP);
            maa_indirect_load<int>(idx, j_tile, idx_tile, cond_res_tile);
            maa_indirect_load<T>(b, idx_tile, b_tile, cond_res_tile);
            maa_alu_scalar<T>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP, cond_res_tile);
            maa_indirect_rmw_vector<T>(a, idx_tile, a_tile, Operation_t::ADD_OP, cond_res_tile);
            wait_ready(j_tile);
            curr_tilej_size = get_tile_size(j_tile);
        } while (curr_tilej_size > 0);
        wait_ready(a_tile);
    }
}

template <class T>
void gather_rmw_cond_indirectrangeloop_cond(int *nodes, int *boundaries, T *a, T *b, int *idx, int min, int max, int stride, const T C, T *cond, const T cond_const) {
    std::cout << "starting gather_rmw_cond_indirectrangeloop_cond min(" << min << "), max(" << max << ")" << std::endl;
    // TODO: parallelize
    for (int i = min; i < max; i += stride) {
        if (cond_const < cond[i]) {
            for (int j = boundaries[nodes[i]]; j < boundaries[nodes[i] + 1]; j++) {
                if (cond_const < cond[j]) {
                    a[idx[j]] += C * b[idx[j]];
                }
            }
        }
    }
}

template <class T>
void gather_rmw_cond_indirectrangeloop_cond_maa(int *nodes, int *boundaries, T *a, T *b, int *idx, int min, int max, int stride, const T C, T *cond, const T cond_const) {
    std::cout << "starting gather_rmw_cond_indirectrangeloop_cond_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

    init_MAA();

    int one_reg_id = get_new_reg<int>(1);
    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(stride);
    int C_reg_id = get_new_reg<T>(C);

    int cond_const_reg_id = get_new_reg<T>(cond_const);
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<T>();
    int a_tile = get_new_tile<T>();
    int condi_tile = get_new_tile<T>();
    int condj_tile = get_new_tile<T>();
    int boundaries0_tile = get_new_tile<int>();
    int boundaries1_tile = get_new_tile<int>();
    int i_tile = get_new_tile<int>();
    int j_tile = get_new_tile<int>();
    int nodes_tile = get_new_tile<int>();
    int condi_res_tile = get_new_tile<uint32_t>();
    int condj_res_tile = get_new_tile<uint32_t>();

    T *b_p = get_cacheable_tile_pointer<T>(b_tile);
    T *a_p = get_cacheable_tile_pointer<T>(a_tile);
    int *i_p = get_cacheable_tile_pointer<int>(i_tile);
    int *j_p = get_cacheable_tile_pointer<int>(j_tile);
    uint32_t *condj_res_p = get_cacheable_tile_pointer<uint32_t>(condj_res_tile);

    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        maa_const(curr_min, min_reg_id);
        maa_stream_load<T>(cond, min_reg_id, max_reg_id, stride_reg_id, condi_tile);
        maa_alu_scalar<T>(condi_tile, cond_const_reg_id, condi_res_tile, Operation_t::GT_OP);
        maa_stream_load<int>(nodes, min_reg_id, max_reg_id, stride_reg_id, nodes_tile, condi_res_tile);
        maa_indirect_load<int>(boundaries, nodes_tile, boundaries0_tile, condi_res_tile);
        maa_indirect_load<int>(&boundaries[1], nodes_tile, boundaries1_tile, condi_res_tile);

        int curr_tilej_size = 0;
        maa_const<int>(0, last_i_reg_id);
        maa_const<int>(-1, last_j_reg_id);
        do {
            maa_range_loop<T>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile, condi_res_tile);
            maa_indirect_load<T>(cond, j_tile, condj_tile);
            maa_alu_scalar<T>(condj_tile, cond_const_reg_id, condj_res_tile, Operation_t::GT_OP);
            maa_indirect_load<int>(idx, j_tile, idx_tile, condj_res_tile);
            maa_indirect_load<T>(b, idx_tile, b_tile, condj_res_tile);
            maa_alu_scalar<T>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP, condj_res_tile);
            maa_indirect_rmw_vector<T>(a, idx_tile, a_tile, Operation_t::ADD_OP, condj_res_tile);
            wait_ready(j_tile);
            curr_tilej_size = get_tile_size(j_tile);
        } while (curr_tilej_size > 0);
        wait_ready(a_tile);
    }
}

template <class T>
void gather_rmw_indirectcond_indirectrangeloop_indirectcond(int *nodes, int *boundaries, T *a, T *b, int *idx, int min, int max, int stride, const T C, T *cond, const T cond_const) {
    std::cout << "starting gather_rmw_indirectcond_indirectrangeloop_indirectcond min(" << min << "), max(" << max << ")" << std::endl;
    // TODO: parallelize
    for (int i = min; i < max; i += stride) {
        if (cond_const < cond[nodes[i]]) {
            for (int j = boundaries[nodes[i]]; j < boundaries[nodes[i] + 1]; j++) {
                if (cond_const < cond[nodes[j]]) {
                    a[idx[j]] += C * b[idx[j]];
                }
            }
        }
    }
}

template <class T>
void gather_rmw_indirectcond_indirectrangeloop_indirectcond_maa(int *nodes, int *boundaries, T *a, T *b, int *idx, int min, int max, int stride, const T C, T *cond, const T cond_const) {
    std::cout << "starting gather_rmw_indirectcond_indirectrangeloop_indirectcond_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

    init_MAA();

    int one_reg_id = get_new_reg<int>(1);
    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(stride);
    int C_reg_id = get_new_reg<T>(C);

    int cond_const_reg_id = get_new_reg<T>(cond_const);
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    int idx_tile = get_new_tile<int>();
    int b_tile = get_new_tile<T>();
    int a_tile = get_new_tile<T>();
    int condi_tile = get_new_tile<T>();
    int condj_tile = get_new_tile<T>();
    int boundaries0_tile = get_new_tile<int>();
    int boundaries1_tile = get_new_tile<int>();
    int i_tile = get_new_tile<int>();
    int j_tile = get_new_tile<int>();
    int nodesi_tile = get_new_tile<int>();
    int nodesj_tile = get_new_tile<int>();
    int condi_res_tile = get_new_tile<uint32_t>();
    int condj_res_tile = get_new_tile<uint32_t>();

    T *b_p = get_cacheable_tile_pointer<T>(b_tile);
    T *a_p = get_cacheable_tile_pointer<T>(a_tile);
    int *i_p = get_cacheable_tile_pointer<int>(i_tile);
    int *j_p = get_cacheable_tile_pointer<int>(j_tile);
    uint32_t *condj_res_p = get_cacheable_tile_pointer<uint32_t>(condj_res_tile);

    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(nodes, min_reg_id, max_reg_id, stride_reg_id, nodesi_tile);
        maa_indirect_load<T>(cond, nodesi_tile, condi_tile);
        maa_alu_scalar<T>(condi_tile, cond_const_reg_id, condi_res_tile, Operation_t::GT_OP);
        maa_indirect_load<int>(boundaries, nodesi_tile, boundaries0_tile, condi_res_tile);
        maa_indirect_load<int>(&boundaries[1], nodesi_tile, boundaries1_tile, condi_res_tile);

        int curr_tilej_size = 0;
        maa_const<int>(0, last_i_reg_id);
        maa_const<int>(-1, last_j_reg_id);
        do {
            maa_range_loop<T>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile, condi_res_tile);
            maa_indirect_load<int>(nodes, j_tile, nodesj_tile);
            maa_indirect_load<T>(cond, nodesj_tile, condj_tile);
            maa_alu_scalar<T>(condj_tile, cond_const_reg_id, condj_res_tile, Operation_t::GT_OP);
            maa_indirect_load<int>(idx, j_tile, idx_tile, condj_res_tile);
            maa_indirect_load<T>(b, idx_tile, b_tile, condj_res_tile);
            maa_alu_scalar<T>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP, condj_res_tile);
            maa_indirect_rmw_vector<T>(a, idx_tile, a_tile, Operation_t::ADD_OP, condj_res_tile);
            wait_ready(j_tile);
            curr_tilej_size = get_tile_size(j_tile);
        } while (curr_tilej_size > 0);
        wait_ready(a_tile);
    }
}

template <class T>
void gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst(int *nodes, int *boundaries, T *a, T *b, T *c, int *idx, int min, int max, int stride, const T C, T *cond, const T cond_const) {
    std::cout << "starting gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst min(" << min << "), max(" << max << ")" << std::endl;
    // TODO: parallelize
    for (int i = min; i < max; i += stride) {
        if (cond_const < cond[nodes[i]]) {
            for (int j = boundaries[nodes[i]]; j < boundaries[nodes[i] + 1]; j++) {
                if (cond_const < cond[nodes[j]]) {
                    c[j] = a[idx[j]];
                    a[idx[j]] += C * b[idx[j]];
                }
            }
        }
    }
}

template <class T>
void gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst_maa(int *nodes, int *boundaries, T *a, T *b, T *c, int *idx, int min, int max, int stride, const T C, T *cond, const T cond_const) {
    std::cout << "starting gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst_maa min(" << min << "), max(" << max << "), tile(" << TILE_SIZE << ")" << std::endl;

    init_MAA();

    int one_reg_id = get_new_reg<int>(1);
    int max_reg_id = get_new_reg<int>(max);
    int min_reg_id = get_new_reg<int>();
    int stride_reg_id = get_new_reg<int>(stride);
    int C_reg_id = get_new_reg<T>(C);

    int cond_const_reg_id = get_new_reg<T>(cond_const);
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    int idx_tile = get_new_tile<int>();
    int c_tile = get_new_tile<T>();
    int b_tile = get_new_tile<T>();
    int a_tile = get_new_tile<T>();
    int condi_tile = get_new_tile<T>();
    int condj_tile = get_new_tile<T>();
    int boundaries0_tile = get_new_tile<int>();
    int boundaries1_tile = get_new_tile<int>();
    int i_tile = get_new_tile<int>();
    int j_tile = get_new_tile<int>();
    int nodesi_tile = get_new_tile<int>();
    int nodesj_tile = get_new_tile<int>();
    int condi_res_tile = get_new_tile<uint32_t>();
    int condj_res_tile = get_new_tile<uint32_t>();

    T *b_p = get_cacheable_tile_pointer<T>(b_tile);
    T *a_p = get_cacheable_tile_pointer<T>(a_tile);
    int *i_p = get_cacheable_tile_pointer<int>(i_tile);
    int *j_p = get_cacheable_tile_pointer<int>(j_tile);
    uint32_t *condj_res_p = get_cacheable_tile_pointer<uint32_t>(condj_res_tile);

    int i_max = (max - min - 1) / stride + 1;
    for (int i_base = 0; i_base < i_max; i_base += TILE_SIZE) {
        int curr_tile_size = i_max - i_base;
        curr_tile_size = curr_tile_size > TILE_SIZE ? TILE_SIZE : curr_tile_size;
        int curr_min = i_base * stride + min;
        maa_const(curr_min, min_reg_id);
        maa_stream_load<int>(nodes, min_reg_id, max_reg_id, stride_reg_id, nodesi_tile);
        maa_indirect_load<T>(cond, nodesi_tile, condi_tile);
        maa_alu_scalar<T>(condi_tile, cond_const_reg_id, condi_res_tile, Operation_t::GT_OP);
        maa_indirect_load<int>(boundaries, nodesi_tile, boundaries0_tile, condi_res_tile);
        maa_indirect_load<int>(&boundaries[1], nodesi_tile, boundaries1_tile, condi_res_tile);

        int curr_tilej_size = 0;
        maa_const<int>(0, last_i_reg_id);
        maa_const<int>(-1, last_j_reg_id);
        do {
            maa_range_loop<T>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile, condi_res_tile);
            maa_indirect_load<int>(nodes, j_tile, nodesj_tile);
            maa_indirect_load<T>(cond, nodesj_tile, condj_tile);
            maa_alu_scalar<T>(condj_tile, cond_const_reg_id, condj_res_tile, Operation_t::GT_OP);
            maa_indirect_load<int>(idx, j_tile, idx_tile, condj_res_tile);
            maa_indirect_load<T>(b, idx_tile, b_tile, condj_res_tile);
            maa_alu_scalar<T>(b_tile, C_reg_id, a_tile, Operation_t::MUL_OP, condj_res_tile);
            maa_indirect_rmw_vector<T>(a, idx_tile, a_tile, Operation_t::ADD_OP, condj_res_tile, c_tile);
            maa_indirect_store_vector<T>(c, j_tile, c_tile, condj_res_tile);
            wait_ready(j_tile);
            curr_tilej_size = get_tile_size(j_tile);
        } while (curr_tilej_size > 0);
        wait_ready(a_tile);
    }
}

/*******************************************************************************/
/*******************************************************************************/
/*                                    MAIN                                     */
/*******************************************************************************/
/*******************************************************************************/

template <class T>
void initializer(int n, int &min, int &max, int &stride, int &const_val, int *&nodes, int *&boundaries, T *&a1, T *&a2, T *&c1, T *&c2, T *&cond, T *&b, int *&idx) {
    stride = rand() % 5 + 1;
    const_val = rand() % 1024;
    nodes = (int *)malloc(sizeof(int) * stride * n);
    boundaries = (int *)malloc(sizeof(int) * (n + 1));
    a1 = (T *)malloc(sizeof(T) * stride * n);
    a2 = (T *)malloc(sizeof(T) * stride * n);
    c1 = (T *)malloc(sizeof(T) * stride * n);
    c2 = (T *)malloc(sizeof(T) * stride * n);
    cond = (T *)malloc(sizeof(T) * stride * n);
    b = (T *)malloc(sizeof(T) * stride * n);
    idx = (int *)malloc(sizeof(int) * stride * n);

    std::cout << "initializing general arrays" << std::endl;
    for (long i = 0; i < (long)n * stride; i += 1) {
        a1[i] = a2[i] = (i * 7) % 1024;
        c1[i] = c2[i] = (i * 11) % 1024;
        cond[i] = rand() % (2 * const_val);
        b[i] = (i * 3) % 1024;
        idx[i] = rand() % (n * stride);
        nodes[i] = rand() % (n);
    }
    int num_nodes = 1;
    boundaries[0] = 0;

    std::cout << "initializing boundaries" << std::endl;
    for (; num_nodes < n; num_nodes++) {
        int tmp = stride + ((rand() % (2 * stride)) - stride);
        assert(tmp >= 0 && tmp < 2 * stride);
        boundaries[num_nodes] = boundaries[num_nodes - 1] + tmp;
        if (boundaries[num_nodes] >= stride * n) {
            break;
        }
    }
    boundaries[num_nodes] = stride * n;

    min = rand() % (num_nodes / 8);
    max = (7 * num_nodes / 8) + rand() % (num_nodes / 8);
}

template <class T>
bool comparer(T *a1, T *a2, long n, std::string kernel) {
    for (long i = 0; i < n; i += 1) {
        if (abs(a1[i] - a2[i]) >= DELTA) {
            std::cout << "Error -- " << kernel << ": " << i << " " << a1[i] << " " << a2[i] << std::endl;
            return false;
        }
    }
    return true;
}

void freer(int *nodes, int *boundaries, void *a1, void *a2, void *c1, void *c2, void *cond, void *b, int *idx) {
    free(nodes);
    free(boundaries);
    free(a1);
    free(a2);
    free(c1);
    free(c2);
    free(cond);
    free(b);
    free(idx);
}

int main(int argc, char *argv[]) {

    if (argc != 5) {
        std::cout << "Usage: " << argv[0] << " <n> [BASE|MAA|CMP] [i32|i64|f32|f64] [kernel|all]" << std::endl;
        return 1;
    }

    srand((unsigned)time(NULL));

    int n = std::stoi(argv[1]);
    std::string mode = argv[2];
    std::string t = argv[3];
    std::string kernel = argv[4];
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
        std::cout << "Error: Unknown mode " << mode << std::endl;
        std::cout << "Usage: " << argv[0] << " <n> [BASE|MAA|CMP] [i32|i64|f32|f64] [kernel|all]" << std::endl;
        return 1;
    }

    int min;
    int max;
    int stride;
    int const_val;
    int *nodes;
    int *boundaries;
    void *a1;
    void *a2;
    void *c1;
    void *c2;
    void *cond;
    void *b;
    int *idx;

    if (t == "i32") {
        initializer<int>(n, min, max, stride, const_val, nodes, boundaries, (int *&)a1, (int *&)a2, (int *&)c1, (int *&)c2, (int *&)cond, (int *&)b, idx);
    } else if (t == "i64") {
        initializer<long>(n, min, max, stride, const_val, nodes, boundaries, (long *&)a1, (long *&)a2, (long *&)c1, (long *&)c2, (long *&)cond, (long *&)b, idx);
    } else if (t == "f32") {
        initializer<float>(n, min, max, stride, const_val, nodes, boundaries, (float *&)a1, (float *&)a2, (float *&)c1, (float *&)c2, (float *&)cond, (float *&)b, idx);
    } else if (t == "f64") {
        initializer<double>(n, min, max, stride, const_val, nodes, boundaries, (double *&)a1, (double *&)a2, (double *&)c1, (double *&)c2, (double *&)cond, (double *&)b, idx);
    } else {
        std::cout << "Error: Unknown type " << t << std::endl;
        std::cout << "Usage: " << argv[0] << " <n> [BASE|MAA|CMP] [i32|i64|f32|f64] [kernel|all]" << std::endl;
#ifdef GEM5
        m5_exit(1);
#endif
        return 1;
    }

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
    m5_clear_mem_region();
    m5_add_mem_region(nodes, nodes + stride * n, 0);
    m5_add_mem_region(boundaries, boundaries + (n + 1), 1);
    if (t == "i32" || t == "f32") {
        m5_add_mem_region(a1, ((int32_t *)a1) + stride * n, 2);
        m5_add_mem_region(a2, ((int32_t *)a2) + stride * n, 3);
        m5_add_mem_region(c1, ((int32_t *)c1) + stride * n, 4);
        m5_add_mem_region(c2, ((int32_t *)c2) + stride * n, 5);
        m5_add_mem_region(cond, ((int32_t *)cond) + stride * n, 6);
        m5_add_mem_region(b, ((int32_t *)b) + stride * n, 7);
    } else {
        m5_add_mem_region(a1, ((int64_t *)a1) + stride * n, 2);
        m5_add_mem_region(a2, ((int64_t *)a2) + stride * n, 3);
        m5_add_mem_region(c1, ((int64_t *)c1) + stride * n, 4);
        m5_add_mem_region(c2, ((int64_t *)c2) + stride * n, 5);
        m5_add_mem_region(cond, ((int64_t *)cond) + stride * n, 6);
        m5_add_mem_region(b, ((int64_t *)b) + stride * n, 7);
    }
    m5_add_mem_region(idx, idx + stride * n, 8);
#endif

    if (kernel == "gather" || kernel == "all") {
        if (maa) {
            if (t == "i32") {
                gather_maa<int32_t>((int32_t *)a2, (int32_t *)b, idx, min, max, stride, 3);
            } else if (t == "i64") {
                gather_maa<int64_t>((int64_t *)a2, (int64_t *)b, idx, min, max, stride, 3);
            } else if (t == "f32") {
                gather_maa<float>((float *)a2, (float *)b, idx, min, max, stride, 3);
            } else if (t == "f64") {
                gather_maa<double>((double *)a2, (double *)b, idx, min, max, stride, 3);
            }
        }
        if (base) {
            if (t == "i32") {
                gather<int32_t>((int32_t *)a1, (int32_t *)b, idx, min, max, stride, 3);
            } else if (t == "i64") {
                gather<int64_t>((int64_t *)a1, (int64_t *)b, idx, min, max, stride, 3);
            } else if (t == "f32") {
                gather<float>((float *)a1, (float *)b, idx, min, max, stride, 3);
            } else if (t == "f64") {
                gather<double>((double *)a1, (double *)b, idx, min, max, stride, 3);
            }
        }
        if (cmp) {
            bool correct;
            if (t == "i32") {
                correct = comparer<int32_t>((int32_t *)a1, (int32_t *)a2, n * stride, "gather");
            } else if (t == "i64") {
                correct = comparer<int64_t>((int64_t *)a1, (int64_t *)a2, n * stride, "gather");
            } else if (t == "f32") {
                correct = comparer<float>((float *)a1, (float *)a2, n * stride, "gather");
            } else if (t == "f64") {
                correct = comparer<double>((double *)a1, (double *)a2, n * stride, "gather");
            }
            if (!correct) {
                freer(nodes, boundaries, a1, a2, c1, c2, cond, b, idx);
#ifdef GEM5
                m5_exit(1);
#endif
                return 1;
            }
            std::cout << "gather correct" << std::endl;
        }
    }

    if (kernel == "scatter" || kernel == "all") {
        if (maa) {
            if (t == "i32") {
                scatter_maa<int32_t>((int32_t *)a2, (int32_t *)b, idx, min, max, stride, 3);
            } else if (t == "i64") {
                scatter_maa<int64_t>((int64_t *)a2, (int64_t *)b, idx, min, max, stride, 3);
            } else if (t == "f32") {
                scatter_maa<float>((float *)a2, (float *)b, idx, min, max, stride, 3);
            } else if (t == "f64") {
                scatter_maa<double>((double *)a2, (double *)b, idx, min, max, stride, 3);
            }
        }
        if (base) {
            if (t == "i32") {
                scatter<int32_t>((int32_t *)a1, (int32_t *)b, idx, min, max, stride, 3);
            } else if (t == "i64") {
                scatter<int64_t>((int64_t *)a1, (int64_t *)b, idx, min, max, stride, 3);
            } else if (t == "f32") {
                scatter<float>((float *)a1, (float *)b, idx, min, max, stride, 3);
            } else if (t == "f64") {
                scatter<double>((double *)a1, (double *)b, idx, min, max, stride, 3);
            }
        }
        if (cmp) {
            bool correct;
            if (t == "i32") {
                correct = comparer<int32_t>((int32_t *)a1, (int32_t *)a2, n * stride, "scatter");
            } else if (t == "i64") {
                correct = comparer<int64_t>((int64_t *)a1, (int64_t *)a2, n * stride, "scatter");
            } else if (t == "f32") {
                correct = comparer<float>((float *)a1, (float *)a2, n * stride, "scatter");
            } else if (t == "f64") {
                correct = comparer<double>((double *)a1, (double *)a2, n * stride, "scatter");
            }
            if (!correct) {
                freer(nodes, boundaries, a1, a2, c1, c2, cond, b, idx);
#ifdef GEM5
                m5_exit(1);
#endif
                return 1;
            }
            std::cout << "scatter correct" << std::endl;
        }
    }

    if (kernel == "rmw" || kernel == "all") {
        if (maa) {
            if (t == "i32") {
                rmw_maa<int32_t>((int32_t *)a2, (int32_t *)b, idx, min, max, stride, 3);
            } else if (t == "i64") {
                rmw_maa<int64_t>((int64_t *)a2, (int64_t *)b, idx, min, max, stride, 3);
            } else if (t == "f32") {
                rmw_maa<float>((float *)a2, (float *)b, idx, min, max, stride, 3);
            } else if (t == "f64") {
                rmw_maa<double>((double *)a2, (double *)b, idx, min, max, stride, 3);
            }
        }
        if (base) {
            if (t == "i32") {
                rmw<int32_t>((int32_t *)a1, (int32_t *)b, idx, min, max, stride, 3);
            } else if (t == "i64") {
                rmw<int64_t>((int64_t *)a1, (int64_t *)b, idx, min, max, stride, 3);
            } else if (t == "f32") {
                rmw<float>((float *)a1, (float *)b, idx, min, max, stride, 3);
            } else if (t == "f64") {
                rmw<double>((double *)a1, (double *)b, idx, min, max, stride, 3);
            }
        }
        if (cmp) {
            bool correct;
            if (t == "i32") {
                correct = comparer<int32_t>((int32_t *)a1, (int32_t *)a2, n * stride, "rmw");
            } else if (t == "i64") {
                correct = comparer<int64_t>((int64_t *)a1, (int64_t *)a2, n * stride, "rmw");
            } else if (t == "f32") {
                correct = comparer<float>((float *)a1, (float *)a2, n * stride, "rmw");
            } else if (t == "f64") {
                correct = comparer<double>((double *)a1, (double *)a2, n * stride, "rmw");
            }
            if (!correct) {
                freer(nodes, boundaries, a1, a2, c1, c2, cond, b, idx);
#ifdef GEM5
                m5_exit(1);
#endif
                return 1;
            }
            std::cout << "rmw correct" << std::endl;
        }
    }

    if (kernel == "gather_scatter" || kernel == "all") {
        if (maa) {
            if (t == "i32") {
                gather_scatter_maa<int32_t>((int32_t *)a2, (int32_t *)b, idx, min, max, stride, 3);
            } else if (t == "i64") {
                gather_scatter_maa<int64_t>((int64_t *)a2, (int64_t *)b, idx, min, max, stride, 3);
            } else if (t == "f32") {
                gather_scatter_maa<float>((float *)a2, (float *)b, idx, min, max, stride, 3);
            } else if (t == "f64") {
                gather_scatter_maa<double>((double *)a2, (double *)b, idx, min, max, stride, 3);
            }
        }
        if (base) {
            if (t == "i32") {
                gather_scatter<int32_t>((int32_t *)a1, (int32_t *)b, idx, min, max, stride, 3);
            } else if (t == "i64") {
                gather_scatter<int64_t>((int64_t *)a1, (int64_t *)b, idx, min, max, stride, 3);
            } else if (t == "f32") {
                gather_scatter<float>((float *)a1, (float *)b, idx, min, max, stride, 3);
            } else if (t == "f64") {
                gather_scatter<double>((double *)a1, (double *)b, idx, min, max, stride, 3);
            }
        }
        if (cmp) {
            bool correct;
            if (t == "i32") {
                correct = comparer<int32_t>((int32_t *)a1, (int32_t *)a2, n * stride, "gather_scatter");
            } else if (t == "i64") {
                correct = comparer<int64_t>((int64_t *)a1, (int64_t *)a2, n * stride, "gather_scatter");
            } else if (t == "f32") {
                correct = comparer<float>((float *)a1, (float *)a2, n * stride, "gather_scatter");
            } else if (t == "f64") {
                correct = comparer<double>((double *)a1, (double *)a2, n * stride, "gather_scatter");
            }
            if (!correct) {
                freer(nodes, boundaries, a1, a2, c1, c2, cond, b, idx);
#ifdef GEM5
                m5_exit(1);
#endif
                return 1;
            }
            std::cout << "gather_scatter correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw" || kernel == "all") {
        if (maa) {
            if (t == "i32") {
                gather_rmw_maa<int32_t>((int32_t *)a2, (int32_t *)b, idx, min, max, stride, 3);
            } else if (t == "i64") {
                gather_rmw_maa<int64_t>((int64_t *)a2, (int64_t *)b, idx, min, max, stride, 3);
            } else if (t == "f32") {
                gather_rmw_maa<float>((float *)a2, (float *)b, idx, min, max, stride, 3);
            } else if (t == "f64") {
                gather_rmw_maa<double>((double *)a2, (double *)b, idx, min, max, stride, 3);
            }
        }
        if (base) {
            if (t == "i32") {
                gather_rmw<int32_t>((int32_t *)a1, (int32_t *)b, idx, min, max, stride, 3);
            } else if (t == "i64") {
                gather_rmw<int64_t>((int64_t *)a1, (int64_t *)b, idx, min, max, stride, 3);
            } else if (t == "f32") {
                gather_rmw<float>((float *)a1, (float *)b, idx, min, max, stride, 3);
            } else if (t == "f64") {
                gather_rmw<double>((double *)a1, (double *)b, idx, min, max, stride, 3);
            }
        }
        if (cmp) {
            bool correct;
            if (t == "i32") {
                correct = comparer<int32_t>((int32_t *)a1, (int32_t *)a2, n * stride, "gather_rmw");
            } else if (t == "i64") {
                correct = comparer<int64_t>((int64_t *)a1, (int64_t *)a2, n * stride, "gather_rmw");
            } else if (t == "f32") {
                correct = comparer<float>((float *)a1, (float *)a2, n * stride, "gather_rmw");
            } else if (t == "f64") {
                correct = comparer<double>((double *)a1, (double *)a2, n * stride, "gather_rmw");
            }
            if (!correct) {
                freer(nodes, boundaries, a1, a2, c1, c2, cond, b, idx);
#ifdef GEM5
                m5_exit(1);
#endif
                return 1;
            }
            std::cout << "gather_rmw correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw_dst" || kernel == "all") {
        if (maa) {
            if (t == "i32") {
                gather_rmw_dst_maa<int32_t>((int32_t *)a2, (int32_t *)b, (int32_t *)c2, idx, min, max, stride, 3);
            } else if (t == "i64") {
                gather_rmw_dst_maa<int64_t>((int64_t *)a2, (int64_t *)b, (int64_t *)c2, idx, min, max, stride, 3);
            } else if (t == "f32") {
                gather_rmw_dst_maa<float>((float *)a2, (float *)b, (float *)c2, idx, min, max, stride, 3);
            } else if (t == "f64") {
                gather_rmw_dst_maa<double>((double *)a2, (double *)b, (double *)c2, idx, min, max, stride, 3);
            }
        }
        if (base) {
            if (t == "i32") {
                gather_rmw_dst<int32_t>((int32_t *)a1, (int32_t *)b, (int32_t *)c1, idx, min, max, stride, 3);
            } else if (t == "i64") {
                gather_rmw_dst<int64_t>((int64_t *)a1, (int64_t *)b, (int64_t *)c1, idx, min, max, stride, 3);
            } else if (t == "f32") {
                gather_rmw_dst<float>((float *)a1, (float *)b, (float *)c1, idx, min, max, stride, 3);
            } else if (t == "f64") {
                gather_rmw_dst<double>((double *)a1, (double *)b, (double *)c1, idx, min, max, stride, 3);
            }
        }
        if (cmp) {
            bool correct;
            if (t == "i32") {
                correct = comparer<int32_t>((int32_t *)a1, (int32_t *)a2, n * stride, "gather_rmw_dst");
                correct = comparer<int32_t>((int32_t *)c1, (int32_t *)c2, n * stride, "gather_rmw_dst");
            } else if (t == "i64") {
                correct = comparer<int64_t>((int64_t *)a1, (int64_t *)a2, n * stride, "gather_rmw_dst");
                correct = comparer<int64_t>((int64_t *)c1, (int64_t *)c2, n * stride, "gather_rmw_dst");
            } else if (t == "f32") {
                correct = comparer<float>((float *)a1, (float *)a2, n * stride, "gather_rmw_dst");
                correct = comparer<float>((float *)c1, (float *)c2, n * stride, "gather_rmw_dst");
            } else if (t == "f64") {
                correct = comparer<double>((double *)a1, (double *)a2, n * stride, "gather_rmw_dst");
                correct = comparer<double>((double *)c1, (double *)c2, n * stride, "gather_rmw_dst");
            }
            if (!correct) {
                freer(nodes, boundaries, a1, a2, c1, c2, cond, b, idx);
#ifdef GEM5
                m5_exit(1);
#endif
                return 1;
            }
            std::cout << "gather_rmw_dst correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw_cond" || kernel == "all") {
        if (maa) {
            if (t == "i32") {
                gather_rmw_cond_maa<int32_t>((int32_t *)a2, (int32_t *)b, idx, min, max, stride, 3, (int32_t *)cond, const_val);
            } else if (t == "i64") {
                gather_rmw_cond_maa<int64_t>((int64_t *)a2, (int64_t *)b, idx, min, max, stride, 3, (int64_t *)cond, const_val);
            } else if (t == "f32") {
                gather_rmw_cond_maa<float>((float *)a2, (float *)b, idx, min, max, stride, 3, (float *)cond, const_val);
            } else if (t == "f64") {
                gather_rmw_cond_maa<double>((double *)a2, (double *)b, idx, min, max, stride, 3, (double *)cond, const_val);
            }
        }
        if (base) {
            if (t == "i32") {
                gather_rmw_cond<int32_t>((int32_t *)a1, (int32_t *)b, idx, min, max, stride, 3, (int32_t *)cond, const_val);
            } else if (t == "i64") {
                gather_rmw_cond<int64_t>((int64_t *)a1, (int64_t *)b, idx, min, max, stride, 3, (int64_t *)cond, const_val);
            } else if (t == "f32") {
                gather_rmw_cond<float>((float *)a1, (float *)b, idx, min, max, stride, 3, (float *)cond, const_val);
            } else if (t == "f64") {
                gather_rmw_cond<double>((double *)a1, (double *)b, idx, min, max, stride, 3, (double *)cond, const_val);
            }
        }
        if (cmp) {
            bool correct;
            if (t == "i32") {
                correct = comparer<int32_t>((int32_t *)a1, (int32_t *)a2, n * stride, "gather_rmw_cond");
            } else if (t == "i64") {
                correct = comparer<int64_t>((int64_t *)a1, (int64_t *)a2, n * stride, "gather_rmw_cond");
            } else if (t == "f32") {
                correct = comparer<float>((float *)a1, (float *)a2, n * stride, "gather_rmw_cond");
            } else if (t == "f64") {
                correct = comparer<double>((double *)a1, (double *)a2, n * stride, "gather_rmw_cond");
            }
            if (!correct) {
                freer(nodes, boundaries, a1, a2, c1, c2, cond, b, idx);
#ifdef GEM5
                m5_exit(1);
#endif
                return 1;
            }
            std::cout << "gather_rmw_cond correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw_directrangeloop_cond" || kernel == "all") {
        if (maa) {
            if (t == "i32") {
                gather_rmw_directrangeloop_cond_maa<int32_t>(boundaries, (int32_t *)a2, (int32_t *)b, idx, min, max, stride, 3, (int32_t *)cond, const_val);
            } else if (t == "i64") {
                gather_rmw_directrangeloop_cond_maa<int64_t>(boundaries, (int64_t *)a2, (int64_t *)b, idx, min, max, stride, 3, (int64_t *)cond, const_val);
            } else if (t == "f32") {
                gather_rmw_directrangeloop_cond_maa<float>(boundaries, (float *)a2, (float *)b, idx, min, max, stride, 3, (float *)cond, const_val);
            } else if (t == "f64") {
                gather_rmw_directrangeloop_cond_maa<double>(boundaries, (double *)a2, (double *)b, idx, min, max, stride, 3, (double *)cond, const_val);
            }
        }
        if (base) {
            if (t == "i32") {
                gather_rmw_directrangeloop_cond<int32_t>(boundaries, (int32_t *)a1, (int32_t *)b, idx, min, max, stride, 3, (int32_t *)cond, const_val);
            } else if (t == "i64") {
                gather_rmw_directrangeloop_cond<int64_t>(boundaries, (int64_t *)a1, (int64_t *)b, idx, min, max, stride, 3, (int64_t *)cond, const_val);
            } else if (t == "f32") {
                gather_rmw_directrangeloop_cond<float>(boundaries, (float *)a1, (float *)b, idx, min, max, stride, 3, (float *)cond, const_val);
            } else if (t == "f64") {
                gather_rmw_directrangeloop_cond<double>(boundaries, (double *)a1, (double *)b, idx, min, max, stride, 3, (double *)cond, const_val);
            }
        }
        if (cmp) {
            bool correct;
            if (t == "i32") {
                correct = comparer<int32_t>((int32_t *)a1, (int32_t *)a2, n * stride, "gather_rmw_directrangeloop_cond");
            } else if (t == "i64") {
                correct = comparer<int64_t>((int64_t *)a1, (int64_t *)a2, n * stride, "gather_rmw_directrangeloop_cond");
            } else if (t == "f32") {
                correct = comparer<float>((float *)a1, (float *)a2, n * stride, "gather_rmw_directrangeloop_cond");
            } else if (t == "f64") {
                correct = comparer<double>((double *)a1, (double *)a2, n * stride, "gather_rmw_directrangeloop_cond");
            }
            if (!correct) {
                freer(nodes, boundaries, a1, a2, c1, c2, cond, b, idx);
#ifdef GEM5
                m5_exit(1);
#endif
                return 1;
            }
            std::cout << "gather_rmw_directrangeloop_cond correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw_indirectrangeloop_cond" || kernel == "all") {
        if (maa) {
            if (t == "i32") {
                gather_rmw_indirectrangeloop_cond_maa<int32_t>(nodes, boundaries, (int32_t *)a2, (int32_t *)b, idx, min, max, stride, 3, (int32_t *)cond, const_val);
            } else if (t == "i64") {
                gather_rmw_indirectrangeloop_cond_maa<int64_t>(nodes, boundaries, (int64_t *)a2, (int64_t *)b, idx, min, max, stride, 3, (int64_t *)cond, const_val);
            } else if (t == "f32") {
                gather_rmw_indirectrangeloop_cond_maa<float>(nodes, boundaries, (float *)a2, (float *)b, idx, min, max, stride, 3, (float *)cond, const_val);
            } else if (t == "f64") {
                gather_rmw_indirectrangeloop_cond_maa<double>(nodes, boundaries, (double *)a2, (double *)b, idx, min, max, stride, 3, (double *)cond, const_val);
            }
        }
        if (base) {
            if (t == "i32") {
                gather_rmw_indirectrangeloop_cond<int32_t>(nodes, boundaries, (int32_t *)a1, (int32_t *)b, idx, min, max, stride, 3, (int32_t *)cond, const_val);
            } else if (t == "i64") {
                gather_rmw_indirectrangeloop_cond<int64_t>(nodes, boundaries, (int64_t *)a1, (int64_t *)b, idx, min, max, stride, 3, (int64_t *)cond, const_val);
            } else if (t == "f32") {
                gather_rmw_indirectrangeloop_cond<float>(nodes, boundaries, (float *)a1, (float *)b, idx, min, max, stride, 3, (float *)cond, const_val);
            } else if (t == "f64") {
                gather_rmw_indirectrangeloop_cond<double>(nodes, boundaries, (double *)a1, (double *)b, idx, min, max, stride, 3, (double *)cond, const_val);
            }
        }
        if (cmp) {
            bool correct;
            if (t == "i32") {
                correct = comparer<int32_t>((int32_t *)a1, (int32_t *)a2, n * stride, "gather_rmw_indirectrangeloop_cond");
            } else if (t == "i64") {
                correct = comparer<int64_t>((int64_t *)a1, (int64_t *)a2, n * stride, "gather_rmw_indirectrangeloop_cond");
            } else if (t == "f32") {
                correct = comparer<float>((float *)a1, (float *)a2, n * stride, "gather_rmw_indirectrangeloop_cond");
            } else if (t == "f64") {
                correct = comparer<double>((double *)a1, (double *)a2, n * stride, "gather_rmw_indirectrangeloop_cond");
            }
            if (!correct) {
                freer(nodes, boundaries, a1, a2, c1, c2, cond, b, idx);
#ifdef GEM5
                m5_exit(1);
#endif
                return 1;
            }
            std::cout << "gather_rmw_indirectrangeloop_cond correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw_cond_indirectrangeloop_cond" || kernel == "all") {
        if (maa) {
            if (t == "i32") {
                gather_rmw_cond_indirectrangeloop_cond_maa<int32_t>(nodes, boundaries, (int32_t *)a2, (int32_t *)b, idx, min, max, stride, 3, (int32_t *)cond, const_val);
            } else if (t == "i64") {
                gather_rmw_cond_indirectrangeloop_cond_maa<int64_t>(nodes, boundaries, (int64_t *)a2, (int64_t *)b, idx, min, max, stride, 3, (int64_t *)cond, const_val);
            } else if (t == "f32") {
                gather_rmw_cond_indirectrangeloop_cond_maa<float>(nodes, boundaries, (float *)a2, (float *)b, idx, min, max, stride, 3, (float *)cond, const_val);
            } else if (t == "f64") {
                gather_rmw_cond_indirectrangeloop_cond_maa<double>(nodes, boundaries, (double *)a2, (double *)b, idx, min, max, stride, 3, (double *)cond, const_val);
            }
        }
        if (base) {
            if (t == "i32") {
                gather_rmw_cond_indirectrangeloop_cond<int32_t>(nodes, boundaries, (int32_t *)a1, (int32_t *)b, idx, min, max, stride, 3, (int32_t *)cond, const_val);
            } else if (t == "i64") {
                gather_rmw_cond_indirectrangeloop_cond<int64_t>(nodes, boundaries, (int64_t *)a1, (int64_t *)b, idx, min, max, stride, 3, (int64_t *)cond, const_val);
            } else if (t == "f32") {
                gather_rmw_cond_indirectrangeloop_cond<float>(nodes, boundaries, (float *)a1, (float *)b, idx, min, max, stride, 3, (float *)cond, const_val);
            } else if (t == "f64") {
                gather_rmw_cond_indirectrangeloop_cond<double>(nodes, boundaries, (double *)a1, (double *)b, idx, min, max, stride, 3, (double *)cond, const_val);
            }
        }
        if (cmp) {
            bool correct;
            if (t == "i32") {
                correct = comparer<int32_t>((int32_t *)a1, (int32_t *)a2, n * stride, "gather_rmw_cond_indirectrangeloop_cond");
            } else if (t == "i64") {
                correct = comparer<int64_t>((int64_t *)a1, (int64_t *)a2, n * stride, "gather_rmw_cond_indirectrangeloop_cond");
            } else if (t == "f32") {
                correct = comparer<float>((float *)a1, (float *)a2, n * stride, "gather_rmw_cond_indirectrangeloop_cond");
            } else if (t == "f64") {
                correct = comparer<double>((double *)a1, (double *)a2, n * stride, "gather_rmw_cond_indirectrangeloop_cond");
            }
            if (!correct) {
                freer(nodes, boundaries, a1, a2, c1, c2, cond, b, idx);
#ifdef GEM5
                m5_exit(1);
#endif
                return 1;
            }
            std::cout << "gather_rmw_cond_indirectrangeloop_cond correct" << std::endl;
        }
    }

    if (kernel == "gather_rmw_indirectcond_indirectrangeloop_indirectcond" || kernel == "all") {
        if (maa) {
            if (t == "i32") {
                gather_rmw_indirectcond_indirectrangeloop_indirectcond_maa<int32_t>(nodes, boundaries, (int32_t *)a2, (int32_t *)b, idx, min, max, stride, 3, (int32_t *)cond, const_val);
            } else if (t == "i64") {
                gather_rmw_indirectcond_indirectrangeloop_indirectcond_maa<int64_t>(nodes, boundaries, (int64_t *)a2, (int64_t *)b, idx, min, max, stride, 3, (int64_t *)cond, const_val);
            } else if (t == "f32") {
                gather_rmw_indirectcond_indirectrangeloop_indirectcond_maa<float>(nodes, boundaries, (float *)a2, (float *)b, idx, min, max, stride, 3, (float *)cond, const_val);
            } else if (t == "f64") {
                gather_rmw_indirectcond_indirectrangeloop_indirectcond_maa<double>(nodes, boundaries, (double *)a2, (double *)b, idx, min, max, stride, 3, (double *)cond, const_val);
            }
        }
        if (base) {
            if (t == "i32") {
                gather_rmw_indirectcond_indirectrangeloop_indirectcond<int32_t>(nodes, boundaries, (int32_t *)a1, (int32_t *)b, idx, min, max, stride, 3, (int32_t *)cond, const_val);
            } else if (t == "i64") {
                gather_rmw_indirectcond_indirectrangeloop_indirectcond<int64_t>(nodes, boundaries, (int64_t *)a1, (int64_t *)b, idx, min, max, stride, 3, (int64_t *)cond, const_val);
            } else if (t == "f32") {
                gather_rmw_indirectcond_indirectrangeloop_indirectcond<float>(nodes, boundaries, (float *)a1, (float *)b, idx, min, max, stride, 3, (float *)cond, const_val);
            } else if (t == "f64") {
                gather_rmw_indirectcond_indirectrangeloop_indirectcond<double>(nodes, boundaries, (double *)a1, (double *)b, idx, min, max, stride, 3, (double *)cond, const_val);
            }
        }
        if (cmp) {
            bool correct;
            if (t == "i32") {
                correct = comparer<int32_t>((int32_t *)a1, (int32_t *)a2, n * stride, "gather_rmw_indirectcond_indirectrangeloop_indirectcond");
            } else if (t == "i64") {
                correct = comparer<int64_t>((int64_t *)a1, (int64_t *)a2, n * stride, "gather_rmw_indirectcond_indirectrangeloop_indirectcond");
            } else if (t == "f32") {
                correct = comparer<float>((float *)a1, (float *)a2, n * stride, "gather_rmw_indirectcond_indirectrangeloop_indirectcond");
            } else if (t == "f64") {
                correct = comparer<double>((double *)a1, (double *)a2, n * stride, "gather_rmw_indirectcond_indirectrangeloop_indirectcond");
            }
            if (!correct) {
                freer(nodes, boundaries, a1, a2, c1, c2, cond, b, idx);
#ifdef GEM5
                m5_exit(1);
#endif
                return 1;
            }
            std::cout << "gather_rmw_indirectcond_indirectrangeloop_indirectcond correct" << std::endl;
        }
    }
    if (kernel == "gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst" || kernel == "all") {
        if (maa) {
            if (t == "i32") {
                gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst_maa<int32_t>(nodes, boundaries, (int32_t *)a2, (int32_t *)b, (int32_t *)c2, idx, min, max, stride, 3, (int32_t *)cond, const_val);
            } else if (t == "i64") {
                gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst_maa<int64_t>(nodes, boundaries, (int64_t *)a2, (int64_t *)b, (int64_t *)c2, idx, min, max, stride, 3, (int64_t *)cond, const_val);
            } else if (t == "f32") {
                gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst_maa<float>(nodes, boundaries, (float *)a2, (float *)b, (float *)c2, idx, min, max, stride, 3, (float *)cond, const_val);
            } else if (t == "f64") {
                gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst_maa<double>(nodes, boundaries, (double *)a2, (double *)b, (double *)c2, idx, min, max, stride, 3, (double *)cond, const_val);
            }
        }
        if (base) {
            if (t == "i32") {
                gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst<int32_t>(nodes, boundaries, (int32_t *)a1, (int32_t *)b, (int32_t *)c1, idx, min, max, stride, 3, (int32_t *)cond, const_val);
            } else if (t == "i64") {
                gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst<int64_t>(nodes, boundaries, (int64_t *)a1, (int64_t *)b, (int64_t *)c1, idx, min, max, stride, 3, (int64_t *)cond, const_val);
            } else if (t == "f32") {
                gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst<float>(nodes, boundaries, (float *)a1, (float *)b, (float *)c1, idx, min, max, stride, 3, (float *)cond, const_val);
            } else if (t == "f64") {
                gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst<double>(nodes, boundaries, (double *)a1, (double *)b, (double *)c1, idx, min, max, stride, 3, (double *)cond, const_val);
            }
        }
        if (cmp) {
            bool correct;
            if (t == "i32") {
                correct = comparer<int32_t>((int32_t *)a1, (int32_t *)a2, n * stride, "gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst");
                correct = comparer<int32_t>((int32_t *)c1, (int32_t *)c2, n * stride, "gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst");
            } else if (t == "i64") {
                correct = comparer<int64_t>((int64_t *)a1, (int64_t *)a2, n * stride, "gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst");
                correct = comparer<int64_t>((int64_t *)c1, (int64_t *)c2, n * stride, "gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst");
            } else if (t == "f32") {
                correct = comparer<float>((float *)a1, (float *)a2, n * stride, "gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst");
                correct = comparer<float>((float *)c1, (float *)c2, n * stride, "gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst");
            } else if (t == "f64") {
                correct = comparer<double>((double *)a1, (double *)a2, n * stride, "gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst");
                correct = comparer<double>((double *)c1, (double *)c2, n * stride, "gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst");
            }
            if (!correct) {
                freer(nodes, boundaries, a1, a2, c1, c2, cond, b, idx);
#ifdef GEM5
                m5_exit(1);
#endif
                return 1;
            }
            std::cout << "gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst correct" << std::endl;
        }
    }
    std::cout << "End of Test, all tests correct!" << std::endl;
    freer(nodes, boundaries, a1, a2, c1, c2, cond, b, idx);
#ifdef GEM5
    m5_exit(0);
#endif
    return 0;
}