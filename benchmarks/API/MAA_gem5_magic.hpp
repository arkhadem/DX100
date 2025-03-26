#include "MAA.hpp"

/*******************************************************************************/
/*******************************************************************************/
/*                            FUNCTIONAL SIMULATION                            */
/*******************************************************************************/
/*******************************************************************************/

#define BASE_ADDR 0x000000000
#define MEM_SIZE 0x400000000       // 16GB
#define SPD_DATA_SIZE 0x000080000  // 512KB = 32 tiles x 4K elements x 4B each element (uint32_t, int32_t, float)
#define SPD_SIZE_SIZE 0x000000040  // 64B = 32 tiles x 2B each tile (uint16_t)
#define SPD_READY_SIZE 0x000000020 // 32B = 32 tiles x 1B each tile (uint8_t)
#define REG_SIZE 0x000000400       // 1KB = 32 registers x 4B each register (uint32_t, int32_t, float)

enum class MAA_data_t {
    INT32,
    FLOAT32,
    MAX_OP
};

void init_MAA() {
    REG_count = 0;
    SPD_count = 0;
    SPD_data_cacheable = (void *)(BASE_ADDR + MEM_SIZE);
    SPD_data_noncacheable = (volatile void *)(BASE_ADDR + MEM_SIZE);
    SPD_size_noncacheable = (volatile uint16_t *)(BASE_ADDR + MEM_SIZE + SPD_DATA_SIZE);
    SPD_ready_noncacheable = (volatile uint8_t *)(BASE_ADDR + MEM_SIZE + SPD_DATA_SIZE + SPD_SIZE_SIZE);
    REG_noncacheable = (volatile void *)(BASE_ADDR + MEM_SIZE + SPD_DATA_SIZE + SPD_SIZE_SIZE + SPD_READY_SIZE);
}

#ifndef GEM5_MAGIC
void m5_maa_magic_stream_load(void *data, MAA_data_t data_type, int min_reg, int max_reg, int stride_reg, int dst_tile);
void m5_maa_magic_stream_load_cond(void *data, MAA_data_t data_type, int min_reg, int max_reg, int stride_reg, int dst_tile, int cond_tile, MAA_cond_t cond_op, int cond_reg, MAA_data_t cond_type);
void m5_maa_magic_indirect_load(void *data, MAA_data_t data_type, int idx_tile, int dst_tile);
void m5_maa_magic_indirect_load_cond(void *data, MAA_data_t data_type, int idx_tile, int dst_tile, int cond_tile, MAA_cond_t cond_op, int cond_reg, MAA_data_t cond_type);
void m5_maa_magic_indirect_store(void *data, MAA_data_t data_type, int idx_tile, int dst_tile, int cond_tile);
void m5_maa_magic_indirect_store_cond(void *data, MAA_data_t data_type, int idx_tile, int dst_tile, int cond_tile, MAA_cond_t cond_op, int cond_reg, MAA_data_t cond_type);
void m5_maa_magic_indirect_rmw(void *data, MAA_data_t data_type, int idx_tile, int dst_tile, RMW_operation_t o_type, int cond_tile);
void m5_maa_magic_indirect_rmw_cond(void *data, MAA_data_t data_type, int idx_tile, int dst_tile, RMW_operation_t o_type, int cond_tile, MAA_cond_t cond_op, int cond_reg, MAA_data_t cond_type);
void m5_maa_magic_range_loop(int last_i_reg, int last_j_reg, int min_tile, int max_tile, int stride_reg, int dst_i_tile, int dst_j_tile);
void m5_maa_magic_range_loop_cond(int last_i_reg, int last_j_reg, int min_tile, int max_tile, int stride_reg, int dst_i_tile, int dst_j_tile, int cond_tile, MAA_cond_t cond_op, int cond_reg, MAA_data_t cond_type);
#endif

template <class T1, class T2>
void maa_stream_load(T1 *data, int min_reg, int max_reg, int stride_reg, int dst_tile, int cond_tile = -1, MAA_cond_t cond_op = MAA_cond_t::MAX_OP, int cond_reg = -1) {
    if (std::is_same<T1, int>::value) {
        if (cond_tile == -1) {
            m5_maa_magic_stream_load(data, MAA_data_t::INT32, min_reg, max_reg, stride_reg, dst_tile);
        } else if (std::is_same<T2, int>::value) {
            m5_maa_magic_stream_load_cond(data, MAA_data_t::INT32, min_reg, max_reg, stride_reg, dst_tile, cond_tile, cond_op, cond_reg, MAA_data_t::INT32);
        } else if (std::is_same<T2, float>::value) {
            m5_maa_magic_stream_load_cond(data, MAA_data_t::INT32, min_reg, max_reg, stride_reg, dst_tile, cond_tile, cond_op, cond_reg, MAA_data_t::FLOAT32);
        } else {
            assert(false);
        }
    } else if (std::is_same<T1, float>::value) {
        if (cond_tile == -1) {
            m5_maa_magic_stream_load(data, MAA_data_t::FLOAT32, min_reg, max_reg, stride_reg, dst_tile);
        } else if (std::is_same<T2, int>::value) {
            m5_maa_magic_stream_load_cond(data, MAA_data_t::FLOAT32, min_reg, max_reg, stride_reg, dst_tile, cond_tile, cond_op, cond_reg, MAA_data_t::INT32);
        } else if (std::is_same<T2, float>::value) {
            m5_maa_magic_stream_load_cond(data, MAA_data_t::FLOAT32, min_reg, max_reg, stride_reg, dst_tile, cond_tile, cond_op, cond_reg, MAA_data_t::FLOAT32);
        } else {
            assert(false);
        }
    } else {
        assert(false);
    }
}
template <class T1, class T2>
void maa_indirect_load(T1 *data, int idx_tile, int dst_tile, int cond_tile = -1, MAA_cond_t cond_op = MAA_cond_t::MAX_OP, int cond_reg = -1) {
    if (std::is_same<T1, int>::value) {
        if (cond_tile == -1) {
            m5_maa_magic_indirect_load(data, MAA_data_t::INT32, idx_tile, dst_tile);
        } else if (std::is_same<T2, int>::value) {
            m5_maa_magic_indirect_load_cond(data, MAA_data_t::INT32, idx_tile, dst_tile, cond_tile, cond_op, cond_reg, MAA_data_t::INT32);
        } else if (std::is_same<T2, float>::value) {
            m5_maa_magic_indirect_load_cond(data, MAA_data_t::INT32, idx_tile, dst_tile, cond_tile, cond_op, cond_reg, MAA_data_t::FLOAT32);
        } else {
            assert(false);
        }
    } else if (std::is_same<T1, float>::value) {
        if (cond_tile == -1) {
            m5_maa_magic_indirect_load(data, MAA_data_t::FLOAT32, idx_tile, dst_tile);
        } else if (std::is_same<T2, int>::value) {
            m5_maa_magic_indirect_load_cond(data, MAA_data_t::FLOAT32, idx_tile, dst_tile, cond_tile, cond_op, cond_reg, MAA_data_t::INT32);
        } else if (std::is_same<T2, float>::value) {
            m5_maa_magic_indirect_load_cond(data, MAA_data_t::FLOAT32, idx_tile, dst_tile, cond_tile, cond_op, cond_reg, MAA_data_t::FLOAT32);
        } else {
            assert(false);
        }
    } else {
        assert(false);
    }
}
template <class T1, class T2>
void maa_indirect_store(T1 *data, int idx_tile, int dst_tile, int cond_tile = -1, MAA_cond_t cond_op = MAA_cond_t::MAX_OP, int cond_reg = -1) {
    if (std::is_same<T1, int>::value) {
        if (cond_tile == -1) {
            m5_maa_magic_indirect_store(data, MAA_data_t::INT32, idx_tile, dst_tile);
        } else if (std::is_same<T2, int>::value) {
            m5_maa_magic_indirect_store_cond(data, MAA_data_t::INT32, idx_tile, dst_tile, cond_tile, cond_op, cond_reg, MAA_data_t::INT32);
        } else if (std::is_same<T2, float>::value) {
            m5_maa_magic_indirect_store_cond(data, MAA_data_t::INT32, idx_tile, dst_tile, cond_tile, cond_op, cond_reg, MAA_data_t::FLOAT32);
        } else {
            assert(false);
        }
    } else if (std::is_same<T1, float>::value) {
        if (cond_tile == -1) {
            m5_maa_magic_indirect_store(data, MAA_data_t::FLOAT32, idx_tile, dst_tile);
        } else if (std::is_same<T2, int>::value) {
            m5_maa_magic_indirect_store_cond(data, MAA_data_t::FLOAT32, idx_tile, dst_tile, cond_tile, cond_op, cond_reg, MAA_data_t::INT32);
        } else if (std::is_same<T2, float>::value) {
            m5_maa_magic_indirect_store_cond(data, MAA_data_t::FLOAT32, idx_tile, dst_tile, cond_tile, cond_op, cond_reg, MAA_data_t::FLOAT32);
        } else {
            assert(false);
        }
    } else {
        assert(false);
    }
}
template <class T1, class T2>
void maa_indirect_rmw(T1 *data, int idx_tile, int dst_tile, RMW_operation_t o_type, int cond_tile = -1, MAA_cond_t cond_op = MAA_cond_t::MAX_OP, int cond_reg = -1) {
    if (std::is_same<T1, int>::value) {
        if (cond_tile == -1) {
            m5_maa_magic_indirect_rmw(data, MAA_data_t::INT32, idx_tile, dst_tile, o_type);
        } else if (std::is_same<T2, int>::value) {
            m5_maa_magic_indirect_rmw_cond(data, MAA_data_t::INT32, idx_tile, dst_tile, o_type, cond_tile, cond_op, cond_reg, MAA_data_t::INT32);
        } else if (std::is_same<T2, float>::value) {
            m5_maa_magic_indirect_rmw_cond(data, MAA_data_t::INT32, idx_tile, dst_tile, o_type, cond_tile, cond_op, cond_reg, MAA_data_t::FLOAT32);
        } else {
            assert(false);
        }
    } else if (std::is_same<T1, float>::value) {
        if (cond_tile == -1) {
            m5_maa_magic_indirect_rmw(data, MAA_data_t::FLOAT32, idx_tile, dst_tile, o_type);
        } else if (std::is_same<T2, int>::value) {
            m5_maa_magic_indirect_rmw_cond(data, MAA_data_t::FLOAT32, idx_tile, dst_tile, o_type, cond_tile, cond_op, cond_reg, MAA_data_t::INT32);
        } else if (std::is_same<T2, float>::value) {
            m5_maa_magic_indirect_rmw_cond(data, MAA_data_t::FLOAT32, idx_tile, dst_tile, o_type, cond_tile, cond_op, cond_reg, MAA_data_t::FLOAT32);
        } else {
            assert(false);
        }
    } else {
        assert(false);
    }
}
// for each tile of i, set last_i_reg to 0 and last_j_reg to -1
template <class T1>
void maa_range_loop(int last_i_reg, int last_j_reg, int min_tile, int max_tile, int stride_reg, int dst_i_tile, int dst_j_tile, int cond_tile = -1, MAA_cond_t cond_op = MAA_cond_t::MAX_OP, int cond_reg = -1) {
    if (cond_tile == -1) {
        m5_maa_magic_range_loop(last_i_reg, last_j_reg, min_tile, max_tile, stride_reg, dst_i_tile, dst_j_tile);
    } else if (std::is_same<T1, int>::value) {
        m5_maa_magic_range_loop_cond(last_i_reg, last_j_reg, min_tile, max_tile, stride_reg, dst_i_tile, dst_j_tile, cond_tile, cond_op, cond_reg, MAA_data_t::INT32);
    } else if (std::is_same<T1, float>::value) {
        m5_maa_magic_range_loop_cond(last_i_reg, last_j_reg, min_tile, max_tile, stride_reg, dst_i_tile, dst_j_tile, cond_tile, cond_op, cond_reg, MAA_data_t::FLOAT32);
    } else {
        assert(false);
    }
}