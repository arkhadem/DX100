#ifndef __MEM_MAA_ALU_HH__
#define __MEM_MAA_ALU_HH__

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include "sim/system.hh"

namespace gem5 {

class MAA;
class Instruction;

class ALUUnit {
public:
    enum class Status : uint8_t {
        Idle = 0,
        Decode = 1,
        Work = 2,
        Finish = 3,
        max
    };

protected:
    std::string status_names[5] = {
        "Idle",
        "Decode",
        "Work",
        "Finish",
        "max"};
    Status state;
    MAA *maa;

public:
    ALUUnit();

    void allocate(MAA *_maa, int _my_alu_id, Cycles _ALU_lane_latency, int _num_ALU_lanes, int _num_tile_elements);

    Status getState() const { return state; }

    void setInstruction(Instruction *_instruction);

    bool scheduleNextExecution(bool force = false);
    void scheduleExecuteInstructionEvent(int latency = 0);

protected:
    Instruction *my_instruction;
    int my_alu_id;
    int my_dst_tile, my_dst_reg, my_cond_tile, my_src1_tile, my_src2_tile;
    bool my_cond_tile_ready, my_src1_tile_ready, my_src2_tile_ready;
    int my_i, my_max;
    int my_input_word_size;
    int my_input_words_per_cl;
    int my_output_word_size;
    int my_output_words_per_cl;
    Cycles ALU_lane_latency;
    int num_ALU_lanes;
    Tick my_SPD_read_finish_tick;
    Tick my_SPD_write_finish_tick;
    Tick my_ALU_finish_tick;
    Tick my_decode_start_tick;
    int num_tile_elements;
    int32_t my_red_i32;
    uint32_t my_red_u32;
    int64_t my_red_i64;
    uint64_t my_red_u64;
    float my_red_f32;
    double my_red_f64;

    void executeInstruction();
    void updateLatency(int num_spd_read_data_accesses,
                       int num_spd_read_cond_accesses,
                       int num_spd_write_accesses,
                       int num_alu_accesses);
    EventFunctionWrapper executeInstructionEvent;
};
} // namespace gem5

#endif // __MEM_MAA_ALU_HH__