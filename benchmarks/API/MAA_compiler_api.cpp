#if !defined(FUNC) && !defined(GEM5) && !defined(GEM5_MAGIC)
#define FUNC
#endif
#include <iostream>
#if defined(FUNC)
#include "MAA_functional.hpp"
#elif defined(GEM5)
#include "MAA_gem5.hpp"
#include <gem5/m5ops.h>
#elif defined(GEM5_MAGIC)
#include "MAA_gem5_magic.hpp"
#endif

extern "C" {
void maa_init() {
    init_MAA();
    std::cerr << "MAA initialized" << std::endl;
}

int maa_getnewtile_i32() {
    int result = get_new_tile<int32_t>();
    std::cerr << "Getting new tile: " << result << std::endl;
    return result;
}

int maa_getnewtile_f32() {
    int result = get_new_tile<float>();
    std::cerr << "Getting new tile: " << result << std::endl;
    return result;
}

int maa_getnewtile_i64() {
    int result = get_new_tile<int64_t>();
    std::cerr << "Getting new tile: " << result << std::endl;
    return result;
}

int maa_getnewtile_f64() {
    int result = get_new_tile<double>();
    std::cerr << "Getting new tile: " << result << std::endl;
    return result;
}

int maa_getnewreg(int data) {
    int result = get_new_reg(data);
    std::cerr << "Getting new reg " << result << std::endl;
    return result;
}

int *maa_getptr_i32(int SPD_id) {
    std::cerr << "Getting pointer" << std::endl;
    return get_cacheable_tile_pointer<int>(SPD_id);
}

void maa_constint_i64_i32(long data, int dst_reg) {
    std::cerr << "Setting constant " << data << " to reg:" << dst_reg << std::endl;
    maa_const<int>(data, dst_reg);
}

void maa_constint_i32_i32(int data, int dst_reg) {
    std::cerr << "Setting constant " << data << " to reg:" << dst_reg << std::endl;
    maa_const<int>(data, dst_reg);
}

void maa_constfp_f32_i32(float data, int dst_reg) {
    std::cerr << "Setting constant " << data << " to reg:" << dst_reg << std::endl;
    maa_const<float>(data, dst_reg);
}

void maa_aluscalar_i32_i32_i32(int src1_tile, int src2_reg, int dst_tile, int op_kind) {
    std::cerr << "ALU scalar: Operation is " << op_kind << std::endl;
    maa_alu_scalar<float>(src1_tile, src2_reg, dst_tile, (Operation_t)op_kind);
}

void maa_streamloadcond_p0_i32_i32_i32_i32_i32(int *data, int lower, int upper, int step, int dst_tile, int cond_tile) {
    std::cerr << "Stream load with condition: " << lower << " " << upper << " " << step << " dest tile is " << dst_tile << " cond tile is " << cond_tile << std::endl;
    maa_stream_load<float>((float *)data, lower, upper, step, dst_tile, cond_tile);
}

void maa_streamload_p0_i32_i32_i32_i32(int *data, int lower, int upper, int step, int dst_tile) {
    std::cerr << "Stream load: " << lower << " " << upper << " " << step << " dest tile is " << dst_tile << std::endl;
    maa_stream_load<float>((float *)data, lower, upper, step, dst_tile);
}

void maa_indirectload_p0_i32_i32(int *data, int idx_tile, int dst_tile) {
    std::cerr << "Indirect load: " << idx_tile << " dest tile is " << dst_tile << std::endl;
    maa_indirect_load<float>((float *)data, idx_tile, dst_tile);
}

void maa_indirectloadcond_p0_i32_i32_i32(int *data, int idx_tile, int dst_tile, int cond_tile) {
    std::cerr << "Indirect load with condition: " << idx_tile << " dest tile is " << dst_tile << " cond tile is " << cond_tile << std::endl;
    maa_indirect_load<float>((float *)data, idx_tile, dst_tile, cond_tile);
}

void maa_indirectstore_i32_p0_i32(int val_tile, int *data, int idx_tile) {
    std::cerr << "Indirect store" << std::endl;
    maa_indirect_store_vector<float>((float *)data, idx_tile, val_tile);
}

void maa_wait_i32(int dest_id) {
    std::cerr << "Waiting for tile: " << dest_id << std::endl;
    wait_ready(dest_id);
}

void maa_indirectrmw_i32_p0_i32(int val_tile, int *data, int idx_tile, int op_kind) {
    std::cerr << "Indirect RMW" << std::endl;
    maa_indirect_rmw_vector<float>((float *)data, idx_tile, val_tile, (Operation_t)op_kind);
}

void maa_indirectrmwcond_i32_p0_i32_i32(int val_tile, int *data, int idx_tile, int cond_tile, int op_kind) {
    std::cerr << "Indirect RMW with condition" << std::endl;
    maa_indirect_rmw_vector<float>((float *)data, idx_tile, val_tile, (Operation_t)op_kind, cond_tile);
}
};