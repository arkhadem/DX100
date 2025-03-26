#pragma once
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <random>
#include <set>

#define DELTA 0.001

#ifndef TILE_SIZE
#define TILE_SIZE 16384
#endif
#ifndef NUM_CORES
#define NUM_CORES 4
#endif
#define NUM_TILES_PER_CORE 8
#define NUM_REGS_PER_CORE 8
#define NUM_TILES (NUM_CORES * NUM_TILES_PER_CORE)
#define NUM_SCALAR_REGS (NUM_CORES * NUM_REGS_PER_CORE)
#define NUM_REGIONS 256

int REG_count;
int SPD_count;

enum class Operation_t {
    ADD_OP = 0,
    SUB_OP = 1,
    MUL_OP = 2,
    DIV_OP = 3,
    MIN_OP = 4,
    MAX_OP = 5,
    AND_OP = 6,
    OR_OP = 7,
    XOR_OP = 8,
    SHL_OP = 9,
    SHR_OP = 10,
    GT_OP = 11,
    GTE_OP = 12,
    LT_OP = 13,
    LTE_OP = 14,
    EQ_OP = 15,
    NE_OP = 16,
    MAX
};

void *SPD_data_cacheable;
volatile void *SPD_data_noncacheable;
volatile uint16_t *SPD_size_noncacheable;
volatile uint16_t *SPD_ready_noncacheable;
volatile void *REG_noncacheable;

template <class T1>
inline T1 *get_cacheable_tile_pointer(int SPD_id) {
    return (T1 *)(&(((uint32_t *)SPD_data_cacheable)[SPD_id * TILE_SIZE]));
}
template <class T1>
inline volatile T1 *get_noncacheable_tile_pointer(int SPD_id) {
    return (T1 *)(&(((uint32_t *)SPD_data_noncacheable)[SPD_id * TILE_SIZE]));
}
