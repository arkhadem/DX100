#pragma once
#include <cassert>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include "MAA.hpp"
// #include "MAA_atomics.hpp"
#include <pthread.h>

/*******************************************************************************/
/*******************************************************************************/
/*                            FUNCTIONAL SIMULATION                            */
/*******************************************************************************/
/*******************************************************************************/

struct memRegion {
    void *start;
    void *end;
    bool valid;
};

memRegion *mem_regions;
pthread_t thread_id;

void alloc_MAA() {
    SPD_data_cacheable = malloc(TILE_SIZE * NUM_TILES * sizeof(int));
    SPD_data_noncacheable = (volatile void *)(SPD_data_cacheable);
    SPD_size_noncacheable = (volatile uint16_t *)malloc(NUM_TILES * sizeof(uint16_t));
    SPD_ready_noncacheable = (volatile uint16_t *)malloc(NUM_TILES * sizeof(uint16_t));
    REG_noncacheable = (volatile void *)malloc(NUM_SCALAR_REGS * sizeof(int));
    for (int i = 0; i < NUM_TILES; i++) {
        SPD_size_noncacheable[i] = 0;
        SPD_ready_noncacheable[i] = 1;
    }
    mem_regions = (memRegion *)malloc(NUM_REGIONS * sizeof(memRegion));
    thread_id = 0;
    for (int i = 0; i < NUM_REGIONS; i++) {
        mem_regions[i].valid = false;
        mem_regions[i].start = NULL;
        mem_regions[i].end = NULL;
    }
}
void m5_add_mem_region(void *start, void *end, int id) {
    assert(id < NUM_REGIONS);
    assert(start != NULL);
    assert(end != NULL);
    mem_regions[id].start = start;
    mem_regions[id].end = end;
    mem_regions[id].valid = true;
    if (thread_id != 0) {
        if (thread_id != pthread_self()) {
            std::cout << "Thread ID: " << thread_id << " pthread_self: " << pthread_self() << std::endl;
            assert(false);
        }
    } else {
        thread_id = pthread_self();
    }
}
void clear_mem_region() {
    for (int i = 0; i < NUM_REGIONS; i++) {
        mem_regions[i].valid = false;
        mem_regions[i].start = NULL;
        mem_regions[i].end = NULL;
    }
    thread_id = 0;
}
inline void init_MAA() {
    std::cout << "Initializing MAA" << std::endl;
    SPD_count = 0;
    REG_count = 0;
}
inline void set_tile_size(int SPD_id, uint16_t size) {
    SPD_size_noncacheable[SPD_id] = size;
}
inline void set_tile_ready(int SPD_id, uint16_t ready) {
    SPD_ready_noncacheable[SPD_id] = ready;
}
inline volatile uint16_t get_tile_ready(int SPD_id) {
    return SPD_ready_noncacheable[SPD_id];
}
inline void wait_ready(int SPD_id) {
    while (get_tile_ready(SPD_id) == 0)
        ;
}
inline uint16_t get_tile_size(int SPD_id) {
    return SPD_size_noncacheable[SPD_id];
}
template <class T1>
inline volatile T1 get_reg(int reg_id) {
    return *((T1 *)(&(((volatile uint32_t *)REG_noncacheable)[reg_id])));
}
template <class T1>
inline void set_reg(int reg_id, T1 data) {
    *((T1 *)(&(((volatile uint32_t *)REG_noncacheable)[reg_id]))) = data;
}
template <class T1>
inline int get_new_reg(T1 data) {
    int num_regs_needed = sizeof(T1) / sizeof(uint32_t);
    assert(num_regs_needed == 1 || num_regs_needed == 2);
    int reg_id = REG_count;
    REG_count += num_regs_needed;
    set_reg<T1>(reg_id, data);
    assert(REG_count <= NUM_SCALAR_REGS);
    return reg_id;
}
template <class T1>
inline int get_new_reg() {
    int num_regs_needed = sizeof(T1) / sizeof(uint32_t);
    assert(num_regs_needed == 1 || num_regs_needed == 2);
    int reg_id = REG_count;
    REG_count += num_regs_needed;
    assert(REG_count <= NUM_SCALAR_REGS);
    return reg_id;
}
template <class T1>
inline int get_new_tile() {
    int num_tiles_needed = sizeof(T1) / sizeof(uint32_t);
    assert(num_tiles_needed == 1 || num_tiles_needed == 2);
    int tile_id = SPD_count;
    SPD_count += num_tiles_needed;
    assert(SPD_count <= NUM_TILES);
    return tile_id;
}
template <class T1>
void maa_const(T1 data, int dst_reg) {
    *((T1 *)(&(((volatile uint32_t *)REG_noncacheable)[dst_reg]))) = data;
}

template <class T1>
void print_tile(int SPD_id) {
    std::cout << "Printing tile " << SPD_id << std::endl;
    T1 *data = get_cacheable_tile_pointer<T1>(SPD_id);
    for (int i = 0; i < get_tile_size(SPD_id); i++) {
        std::cout << "[" << i << "]=" << data[i] << std::endl;
    }
}
float alu(float op1, float op2, Operation_t op) {
    switch (op) {
    case Operation_t::GT_OP:
        return op1 > op2 ? 1 : 0;
    case Operation_t::GTE_OP:
        return op1 >= op2 ? 1 : 0;
    case Operation_t::LT_OP:
        return op1 < op2 ? 1 : 0;
    case Operation_t::LTE_OP:
        return op1 <= op2 ? 1 : 0;
    case Operation_t::EQ_OP:
        return op1 == op2 ? 1 : 0;
    case Operation_t::NE_OP:
        return op1 != op2 ? 1 : 0;
    case Operation_t::ADD_OP:
        return op1 + op2;
    case Operation_t::SUB_OP:
        return op1 - op2;
    case Operation_t::MUL_OP:
        return op1 * op2;
    case Operation_t::DIV_OP:
        return op1 / op2;
    case Operation_t::MIN_OP:
        return op1 < op2 ? op1 : op2;
    case Operation_t::MAX_OP:
        return op1 > op2 ? op1 : op2;
    default:
        assert(false);
    }
}
double alu(double op1, double op2, Operation_t op) {
    switch (op) {
    case Operation_t::GT_OP:
        return op1 > op2 ? 1 : 0;
    case Operation_t::GTE_OP:
        return op1 >= op2 ? 1 : 0;
    case Operation_t::LT_OP:
        return op1 < op2 ? 1 : 0;
    case Operation_t::LTE_OP:
        return op1 <= op2 ? 1 : 0;
    case Operation_t::EQ_OP:
        return op1 == op2 ? 1 : 0;
    case Operation_t::NE_OP:
        return op1 != op2 ? 1 : 0;
    case Operation_t::ADD_OP:
        return op1 + op2;
    case Operation_t::SUB_OP:
        return op1 - op2;
    case Operation_t::MUL_OP:
        return op1 * op2;
    case Operation_t::DIV_OP:
        return op1 / op2;
    case Operation_t::MIN_OP:
        return op1 < op2 ? op1 : op2;
    case Operation_t::MAX_OP:
        return op1 > op2 ? op1 : op2;
    default:
        assert(false);
    }
}
uint32_t alu(uint32_t op1, uint32_t op2, Operation_t op) {
    switch (op) {
    case Operation_t::GT_OP:
        return op1 > op2 ? 1 : 0;
    case Operation_t::GTE_OP:
        return op1 >= op2 ? 1 : 0;
    case Operation_t::LT_OP:
        return op1 < op2 ? 1 : 0;
    case Operation_t::LTE_OP:
        return op1 <= op2 ? 1 : 0;
    case Operation_t::EQ_OP:
        return op1 == op2 ? 1 : 0;
    case Operation_t::NE_OP:
        return op1 != op2 ? 1 : 0;
    case Operation_t::ADD_OP:
        return op1 + op2;
    case Operation_t::SUB_OP:
        return op1 - op2;
    case Operation_t::MUL_OP:
        return op1 * op2;
    case Operation_t::DIV_OP:
        return op1 / op2;
    case Operation_t::MIN_OP:
        return op1 < op2 ? op1 : op2;
    case Operation_t::MAX_OP:
        return op1 > op2 ? op1 : op2;
    case Operation_t::AND_OP:
        return op1 & op2;
    case Operation_t::OR_OP:
        return op1 | op2;
    case Operation_t::XOR_OP:
        return op1 ^ op2;
    case Operation_t::SHL_OP:
        return op1 << op2;
    case Operation_t::SHR_OP:
        return op1 >> op2;
    default:
        assert(false);
    }
}
uint64_t alu(uint64_t op1, uint64_t op2, Operation_t op) {
    switch (op) {
    case Operation_t::GT_OP:
        return op1 > op2 ? 1 : 0;
    case Operation_t::GTE_OP:
        return op1 >= op2 ? 1 : 0;
    case Operation_t::LT_OP:
        return op1 < op2 ? 1 : 0;
    case Operation_t::LTE_OP:
        return op1 <= op2 ? 1 : 0;
    case Operation_t::EQ_OP:
        return op1 == op2 ? 1 : 0;
    case Operation_t::NE_OP:
        return op1 != op2 ? 1 : 0;
    case Operation_t::ADD_OP:
        return op1 + op2;
    case Operation_t::SUB_OP:
        return op1 - op2;
    case Operation_t::MUL_OP:
        return op1 * op2;
    case Operation_t::DIV_OP:
        return op1 / op2;
    case Operation_t::MIN_OP:
        return op1 < op2 ? op1 : op2;
    case Operation_t::MAX_OP:
        return op1 > op2 ? op1 : op2;
    case Operation_t::AND_OP:
        return op1 & op2;
    case Operation_t::OR_OP:
        return op1 | op2;
    case Operation_t::XOR_OP:
        return op1 ^ op2;
    case Operation_t::SHL_OP:
        return op1 << op2;
    case Operation_t::SHR_OP:
        return op1 >> op2;
    default:
        assert(false);
    }
}
int32_t alu(int32_t op1, int32_t op2, Operation_t op) {
    switch (op) {
    case Operation_t::GT_OP:
        return op1 > op2 ? 1 : 0;
    case Operation_t::GTE_OP:
        return op1 >= op2 ? 1 : 0;
    case Operation_t::LT_OP:
        return op1 < op2 ? 1 : 0;
    case Operation_t::LTE_OP:
        return op1 <= op2 ? 1 : 0;
    case Operation_t::EQ_OP:
        return op1 == op2 ? 1 : 0;
    case Operation_t::NE_OP:
        return op1 != op2 ? 1 : 0;
    case Operation_t::ADD_OP:
        return op1 + op2;
    case Operation_t::SUB_OP:
        return op1 - op2;
    case Operation_t::MUL_OP:
        return op1 * op2;
    case Operation_t::DIV_OP:
        return op1 / op2;
    case Operation_t::MIN_OP:
        return op1 < op2 ? op1 : op2;
    case Operation_t::MAX_OP:
        return op1 > op2 ? op1 : op2;
    case Operation_t::AND_OP:
        return op1 & op2;
    case Operation_t::OR_OP:
        return op1 | op2;
    case Operation_t::XOR_OP:
        return op1 ^ op2;
    case Operation_t::SHL_OP:
        return op1 << op2;
    case Operation_t::SHR_OP:
        return op1 >> op2;
    default:
        assert(false);
    }
}
int64_t alu(int64_t op1, int64_t op2, Operation_t op) {
    switch (op) {
    case Operation_t::GT_OP:
        return op1 > op2 ? 1 : 0;
    case Operation_t::GTE_OP:
        return op1 >= op2 ? 1 : 0;
    case Operation_t::LT_OP:
        return op1 < op2 ? 1 : 0;
    case Operation_t::LTE_OP:
        return op1 <= op2 ? 1 : 0;
    case Operation_t::EQ_OP:
        return op1 == op2 ? 1 : 0;
    case Operation_t::NE_OP:
        return op1 != op2 ? 1 : 0;
    case Operation_t::ADD_OP:
        return op1 + op2;
    case Operation_t::SUB_OP:
        return op1 - op2;
    case Operation_t::MUL_OP:
        return op1 * op2;
    case Operation_t::DIV_OP:
        return op1 / op2;
    case Operation_t::MIN_OP:
        return op1 < op2 ? op1 : op2;
    case Operation_t::MAX_OP:
        return op1 > op2 ? op1 : op2;
    case Operation_t::AND_OP:
        return op1 & op2;
    case Operation_t::OR_OP:
        return op1 | op2;
    case Operation_t::XOR_OP:
        return op1 ^ op2;
    case Operation_t::SHL_OP:
        return op1 << op2;
    case Operation_t::SHR_OP:
        return op1 >> op2;
    default:
        assert(false);
    }
}
template <class T1>
inline void maa_alu_scalar(int src1_tile, int src2_reg, int dst_tile, Operation_t op, int cond_tile = -1) {
    T1 *dst_T = get_cacheable_tile_pointer<T1>(dst_tile);
    uint32_t *dst_u32 = get_cacheable_tile_pointer<uint32_t>(dst_tile);
    T1 *src1 = get_cacheable_tile_pointer<T1>(src1_tile);
    T1 src2 = get_reg<T1>(src2_reg);
    int src_size = get_tile_size(src1_tile);
    uint32_t *cond_array = nullptr;
    if (cond_tile != -1)
        cond_array = get_cacheable_tile_pointer<uint32_t>(cond_tile);
    switch (op) {
    case Operation_t::GT_OP:
    case Operation_t::GTE_OP:
    case Operation_t::LT_OP:
    case Operation_t::LTE_OP:
    case Operation_t::NE_OP:
    case Operation_t::EQ_OP: {
#pragma omp parallel for
        for (int i = 0; i < src_size; i++) {
            if (cond_tile == -1 || cond_array[i]) {
                dst_u32[i] = (uint32_t)alu(src1[i], src2, op);
            }
        }
        break;
    }
    case Operation_t::ADD_OP:
    case Operation_t::SUB_OP:
    case Operation_t::MUL_OP:
    case Operation_t::DIV_OP:
    case Operation_t::MIN_OP:
    case Operation_t::MAX_OP:
    case Operation_t::AND_OP:
    case Operation_t::OR_OP:
    case Operation_t::XOR_OP:
    case Operation_t::SHL_OP:
    case Operation_t::SHR_OP: {
#pragma omp parallel for
        for (int i = 0; i < src_size; i++) {
            if (cond_tile == -1 || cond_array[i]) {
                dst_T[i] = alu(src1[i], src2, op);
            }
        }
        break;
    }
    default:
        assert(false);
    }
    set_tile_size(dst_tile, src_size);
    set_tile_ready(dst_tile, 1);
}
template <class T1>
inline void maa_alu_reduce(int src1_tile, int dst_reg, Operation_t op, int cond_tile = -1) {
    T1 result;
    T1 *src1 = get_cacheable_tile_pointer<T1>(src1_tile);
    int src_size = get_tile_size(src1_tile);
    uint32_t *cond_array = nullptr;
    if (cond_tile != -1)
        cond_array = get_cacheable_tile_pointer<uint32_t>(cond_tile);
    switch (op) {
    case Operation_t::OR_OP:
    case Operation_t::ADD_OP:
    case Operation_t::SUB_OP: {
        result = 0;
        break;
    }
    case Operation_t::MUL_OP:
    case Operation_t::DIV_OP: {
        result = 1;
        break;
    }
    case Operation_t::MIN_OP: {
        result = std::numeric_limits<T1>::max();
        break;
    }
    case Operation_t::MAX_OP: {
        result = std::numeric_limits<T1>::min();
        break;
    }
    case Operation_t::AND_OP: {
        if (sizeof(T1) == 1)
            result = 0xFF;
        else if (sizeof(T1) == 2)
            result = 0xFFFF;
        else if (sizeof(T1) == 4)
            result = 0xFFFFFFFF;
        else if (sizeof(T1) == 8)
            result = 0xFFFFFFFFFFFFFFFF;
        break;
    }
    default:
        assert(false);
    }
    // #pragma omp parallel for
    for (int i = 0; i < src_size; i++) {
        if (cond_tile == -1 || cond_array[i]) {
            result = alu(src1[i], result, op);
        }
    }
    maa_const<T1>(result, dst_reg);
    set_tile_ready(src1_tile, 1);
}
template <class T1>
inline void maa_alu_vector(int src1_tile, int src2_tile, int dst_tile, Operation_t op, int cond_tile = -1) {
    T1 *dst_T = get_cacheable_tile_pointer<T1>(dst_tile);
    uint32_t *dst_u32 = get_cacheable_tile_pointer<uint32_t>(dst_tile);
    T1 *src1 = get_cacheable_tile_pointer<T1>(src1_tile);
    T1 *src2 = get_cacheable_tile_pointer<T1>(src2_tile);
    int src_size = get_tile_size(src1_tile);
    uint32_t *cond_array = nullptr;
    if (cond_tile != -1)
        cond_array = get_cacheable_tile_pointer<uint32_t>(cond_tile);
    switch (op) {
    case Operation_t::GT_OP:
    case Operation_t::GTE_OP:
    case Operation_t::LT_OP:
    case Operation_t::LTE_OP:
    case Operation_t::EQ_OP: {
#pragma omp parallel for
        for (int i = 0; i < src_size; i++) {
            if (cond_tile == -1 || cond_array[i]) {
                dst_u32[i] = (uint32_t)alu(src1[i], src2[i], op);
            }
        }
        break;
    }
    case Operation_t::ADD_OP:
    case Operation_t::SUB_OP:
    case Operation_t::MUL_OP:
    case Operation_t::DIV_OP:
    case Operation_t::MIN_OP:
    case Operation_t::MAX_OP:
    case Operation_t::AND_OP:
    case Operation_t::OR_OP:
    case Operation_t::XOR_OP:
    case Operation_t::SHL_OP:
    case Operation_t::SHR_OP: {
#pragma omp parallel for
        for (int i = 0; i < src_size; i++) {
            if (cond_tile == -1 || cond_array[i]) {
                dst_T[i] = alu(src1[i], src2[i], op);
            }
        }
        break;
    }
    default:
        assert(false);
    }
    set_tile_size(dst_tile, src_size);
    set_tile_ready(dst_tile, 1);
}
int8_t get_region(void *data) {
    if (thread_id != pthread_self()) {
        return -1;
    }
    for (int i = 0; i < NUM_REGIONS; i++) {
        if (mem_regions[i].valid && data >= mem_regions[i].start && data < mem_regions[i].end) {
            return i;
        }
    }
    return -1;
}
bool check_region(int8_t region, void *data) {
    if (region != -1) {
        assert(mem_regions[region].valid);
        if (data < mem_regions[region].start || data >= mem_regions[region].end) {
            printf("Region Error: data: %p, min: %p, max: %p, reg: %d\n", data, mem_regions[region].start, mem_regions[region].end, region);
            return false;
        }
    }
    return true;
}
template <class T1>
inline void maa_stream_load(T1 *data, int min_reg, int max_reg, int stride_reg, int dst_tile, int cond_tile = -1) {
    T1 *dst = get_cacheable_tile_pointer<T1>(dst_tile);
    uint32_t *cond_array = nullptr;
    if (cond_tile != -1)
        cond_array = get_cacheable_tile_pointer<uint32_t>(cond_tile);
    int min = get_reg<int>(min_reg);
    int max = get_reg<int>(max_reg);
    int stride = get_reg<int>(stride_reg);
    int idx = 0;
    int8_t region = get_region(data);
    // TODO: fix this for OMP
    // #pragma omp parallel for
    for (int i = min; i < max && idx < TILE_SIZE; i += stride, idx++) {
        if (cond_tile == -1 || cond_array[idx]) {
            assert(check_region(region, data + i));
            dst[idx] = data[i];
        }
    }
    set_tile_size(dst_tile, idx);
    set_tile_ready(dst_tile, 1);
}
template <class T1>
inline void maa_stream_store(T1 *data, int min_reg, int max_reg, int stride_reg, int src_tile, int cond_tile = -1) {
    T1 *src = get_cacheable_tile_pointer<T1>(src_tile);
    uint32_t *cond_array = nullptr;
    if (cond_tile != -1)
        cond_array = get_cacheable_tile_pointer<uint32_t>(cond_tile);
    int min = get_reg<int>(min_reg);
    int max = get_reg<int>(max_reg);
    int stride = get_reg<int>(stride_reg);
    int idx = 0;
    int8_t region = get_region(data);
    // stores cannot use openmp
    for (int i = min; i < max && idx < TILE_SIZE; i += stride, idx++) {
        if (cond_tile == -1 || cond_array[idx]) {
            assert(check_region(region, data + i));
            data[i] = src[idx];
        }
    }
    set_tile_ready(src_tile, 1);
}
template <class T1>
inline void maa_indirect_load(T1 *data, int idx_tile, int dst_tile, int cond_tile = -1) {
    T1 *dst = get_cacheable_tile_pointer<T1>(dst_tile);
    int *indices = get_cacheable_tile_pointer<int>(idx_tile);
    int index_size = get_tile_size(idx_tile);
    uint32_t *cond_array = nullptr;
    if (cond_tile != -1)
        cond_array = get_cacheable_tile_pointer<uint32_t>(cond_tile);
    int8_t region = get_region(data);
    // #pragma omp parallel for
    for (int idx = 0; idx < index_size; idx++) {
        if (cond_tile == -1 || cond_array[idx]) {
            assert(check_region(region, data + indices[idx]));
            dst[idx] = data[indices[idx]];
        }
    }
    set_tile_size(dst_tile, index_size);
    set_tile_ready(dst_tile, 1);
}
template <class T1>
inline void maa_indirect_store_vector(T1 *data, int idx_tile, int src_tile, int cond_tile = -1, int dst_tile = -1) {
    volatile T1 *src = get_cacheable_tile_pointer<T1>(src_tile);
    int *indices = get_cacheable_tile_pointer<int>(idx_tile);
    int index_size = get_tile_size(idx_tile);
    uint32_t *cond_array = nullptr;
    volatile T1 *dst = nullptr;
    if (dst_tile != -1) {
        dst = get_cacheable_tile_pointer<T1>(dst_tile);
    }
    if (cond_tile != -1)
        cond_array = get_cacheable_tile_pointer<uint32_t>(cond_tile);
    // stores cannot use openmp
    int8_t region = get_region(data);
    for (int idx = 0; idx < index_size; idx++) {
        if (cond_tile == -1 || cond_array[idx]) {
            assert(check_region(region, data + indices[idx]));
            if (dst_tile != -1) {
                dst[idx] = data[indices[idx]];
            }
            data[indices[idx]] = src[idx];
        }
    }
    set_tile_ready(src_tile, 1);
    if (dst_tile != -1) {
        set_tile_size(dst_tile, index_size);
        set_tile_ready(dst_tile, 1);
    }
}
template <class T1>
inline void maa_indirect_store_scalar(T1 *data, int idx_tile, int src_reg, int cond_tile = -1, int dst_tile = -1) {
    T1 src = get_reg<T1>(src_reg);
    int *indices = get_cacheable_tile_pointer<int>(idx_tile);
    int index_size = get_tile_size(idx_tile);
    uint32_t *cond_array = nullptr;
    volatile T1 *dst = nullptr;
    if (dst_tile != -1) {
        dst = get_cacheable_tile_pointer<T1>(dst_tile);
    }
    if (cond_tile != -1)
        cond_array = get_cacheable_tile_pointer<uint32_t>(cond_tile);
    // stores cannot use openmp
    int8_t region = get_region(data);
    for (int idx = 0; idx < index_size; idx++) {
        if (cond_tile == -1 || cond_array[idx]) {
            assert(check_region(region, data + indices[idx]));
            if (dst_tile != -1) {
                dst[idx] = data[indices[idx]];
            }
            data[indices[idx]] = src;
        }
    }
    if (dst_tile != -1) {
        set_tile_size(dst_tile, index_size);
        set_tile_ready(dst_tile, 1);
    }
}
template <class T1>
inline void maa_indirect_rmw_vector(T1 *data, int idx_tile, int src_tile, Operation_t o_type, int cond_tile = -1, int dst_tile = -1) {
    volatile T1 *src = get_cacheable_tile_pointer<T1>(src_tile);
    int *indices = get_cacheable_tile_pointer<int>(idx_tile);
    int index_size = get_tile_size(idx_tile);
    assert(index_size == get_tile_size(src_tile));
    uint32_t *cond_array = nullptr;
    volatile T1 *dst;
    if (cond_tile != -1) {
        cond_array = get_cacheable_tile_pointer<uint32_t>(cond_tile);
        assert(index_size == get_tile_size(cond_tile));
    }
    if (dst_tile != -1) {
        dst = get_cacheable_tile_pointer<T1>(dst_tile);
    }

    int8_t region = get_region(data);
    switch (o_type) {
    case Operation_t::ADD_OP:
    case Operation_t::SUB_OP:
    case Operation_t::MUL_OP:
    case Operation_t::DIV_OP:
    case Operation_t::MIN_OP:
    case Operation_t::MAX_OP: {
        for (int idx = 0; idx < index_size; idx++) {
            if (cond_tile == -1 || cond_array[idx]) {
                assert(check_region(region, data + indices[idx]));
                if (dst_tile != -1) {
                    dst[idx] = data[indices[idx]];
                }
                data[indices[idx]] = alu(data[indices[idx]], src[idx], o_type);
            }
        }
        break;
    }
    default:
        assert(false);
    }
    set_tile_ready(src_tile, 1);
    if (dst_tile != -1) {
        set_tile_ready(dst_tile, 1);
        set_tile_size(dst_tile, index_size);
    }
}
template <class T1>
inline void maa_indirect_rmw_scalar(T1 *data, int idx_tile, int src_reg, Operation_t o_type, int cond_tile = -1, int dst_tile = -1) {
    T1 src = get_reg<T1>(src_reg);
    int *indices = get_cacheable_tile_pointer<int>(idx_tile);
    int index_size = get_tile_size(idx_tile);
    uint32_t *cond_array = nullptr;
    volatile T1 *dst;
    if (cond_tile != -1) {
        cond_array = get_cacheable_tile_pointer<uint32_t>(cond_tile);
        assert(index_size == get_tile_size(cond_tile));
    }
    if (dst_tile != -1) {
        dst = get_cacheable_tile_pointer<T1>(dst_tile);
    }

    int8_t region = get_region(data);
    switch (o_type) {
    case Operation_t::ADD_OP:
    case Operation_t::SUB_OP:
    case Operation_t::MUL_OP:
    case Operation_t::DIV_OP:
    case Operation_t::MIN_OP:
    case Operation_t::MAX_OP: {
        for (int idx = 0; idx < index_size; idx++) {
            if (cond_tile == -1 || cond_array[idx]) {
                assert(check_region(region, data + indices[idx]));
                if (dst_tile != -1) {
                    dst[idx] = data[indices[idx]];
                }
                data[indices[idx]] = alu(data[indices[idx]], src, o_type);
            }
        }
        break;
    }
    default:
        assert(false);
    }
    if (dst_tile != -1) {
        set_tile_ready(dst_tile, 1);
        set_tile_size(dst_tile, index_size);
    }
}
// for each tile of i, set last_i_reg to 0 and last_j_reg to -1
template <class T1>
inline void maa_range_loop(int last_i_reg, int last_j_reg, int min_tile, int max_tile, int stride_reg, int dst_i_tile, int dst_j_tile, int cond_tile = -1) {
    int *dst_j = get_cacheable_tile_pointer<int>(dst_j_tile);
    int *dst_i = get_cacheable_tile_pointer<int>(dst_i_tile);
    int *mins = get_cacheable_tile_pointer<int>(min_tile);
    int *maxs = get_cacheable_tile_pointer<int>(max_tile);
    int min_size = get_tile_size(min_tile);
    int max_size = get_tile_size(max_tile);
    assert(min_size == max_size);
    int stride = get_reg<int>(stride_reg);
    uint32_t *cond_array = nullptr;
    if (cond_tile != -1)
        cond_array = get_cacheable_tile_pointer<uint32_t>(cond_tile);
    int i = get_reg<int>(last_i_reg);
    int j = get_reg<int>(last_j_reg);
    int idxj = 0;
    for (; i < min_size && idxj < TILE_SIZE; i++) {
        if (cond_tile == -1 || cond_array[i]) {
            if (j == -1) {
                j = mins[i];
            }
            // TODO: fix this for OMP
            // #pragma omp parallel for
            for (; j < maxs[i] && idxj < TILE_SIZE; j += stride, idxj++) {
                // printf("R[%d] %s: [%d-%d-%d][%d-%d-%d] inserted!\n", 0, __func__, 0, i, min_size, mins[i], j, maxs[i]);
                dst_i[idxj] = i;
                dst_j[idxj] = j;
            }
            if (j >= maxs[i]) {
                j = -1;
            } else if (idxj == TILE_SIZE) {
                break;
            }
        }
    }
    set_reg<int>(last_i_reg, i);
    set_reg<int>(last_j_reg, j);
    set_tile_size(dst_j_tile, idxj == -1 ? TILE_SIZE : idxj);
    set_tile_ready(dst_j_tile, 1);
    set_tile_size(dst_i_tile, idxj == -1 ? TILE_SIZE : idxj);
    set_tile_ready(dst_i_tile, 1);
}