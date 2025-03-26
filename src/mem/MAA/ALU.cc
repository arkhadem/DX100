#include "mem/MAA/ALU.hh"
#include "mem/MAA/MAA.hh"
#include "mem/MAA/IF.hh"
#include "mem/MAA/SPD.hh"
#include "base/trace.hh"
#include "debug/MAAALU.hh"
#include "debug/MAATrace.hh"
#include <cassert>

#ifndef TRACING_ON
#define TRACING_ON 1
#endif

namespace gem5 {
///////////////
//
// ALU ACCESS UNIT
//
///////////////
ALUUnit::ALUUnit()
    : executeInstructionEvent([this] { executeInstruction(); }, name()) {
    my_dst_tile = -1;
    my_instruction = nullptr;
}
void ALUUnit::allocate(MAA *_maa, int _my_alu_id, Cycles _ALU_lane_latency, int _num_ALU_lanes, int _num_tile_elements) {
    state = Status::Idle;
    maa = _maa;
    my_alu_id = _my_alu_id;
    ALU_lane_latency = _ALU_lane_latency;
    num_ALU_lanes = _num_ALU_lanes;
    num_tile_elements = _num_tile_elements;
    my_instruction = nullptr;
}
void ALUUnit::updateLatency(int num_spd_read_data_accesses,
                            int num_spd_read_cond_accesses,
                            int num_spd_write_accesses,
                            int num_alu_accesses) {
    if (num_spd_read_data_accesses != 0 || num_spd_read_cond_accesses != 0) {
        // XByte -- 64/X bytes per SPD access
        num_spd_read_data_accesses = getCeiling(num_spd_read_data_accesses, my_input_words_per_cl);
        // 4Byte conditions -- 16 bytes per SPD access
        num_spd_read_cond_accesses = getCeiling(num_spd_read_cond_accesses, 16);
        Cycles get_data_latency = maa->spd->getDataLatency(num_spd_read_data_accesses + num_spd_read_cond_accesses);
        my_SPD_read_finish_tick = maa->getClockEdge(get_data_latency);
        (*maa->stats.ALU_CyclesSPDReadAccess[my_alu_id]) += get_data_latency;
    }
    if (num_spd_write_accesses != 0) {
        // XByte -- 64/X bytes per SPD access
        Cycles set_data_latency = maa->spd->setDataLatency(my_dst_tile, getCeiling(num_spd_write_accesses, my_output_words_per_cl));
        my_SPD_write_finish_tick = maa->getClockEdge(set_data_latency);
        (*maa->stats.ALU_CyclesSPDWriteAccess[my_alu_id]) += set_data_latency;
    }
    if (num_alu_accesses != 0) {
        // ALU operations
        Cycles ALU_latency = Cycles(getCeiling(num_alu_accesses, num_ALU_lanes) * ALU_lane_latency);
        if (my_ALU_finish_tick < curTick())
            my_ALU_finish_tick = maa->getClockEdge(ALU_latency);
        else
            my_ALU_finish_tick += maa->getCyclesToTicks(ALU_latency);
        (*maa->stats.ALU_CyclesCompute[my_alu_id]) += ALU_latency;
    }
}
bool ALUUnit::scheduleNextExecution(bool force) {
    Tick finish_tick = std::max(std::max(my_SPD_read_finish_tick, my_SPD_write_finish_tick), my_ALU_finish_tick);
    if (curTick() < finish_tick) {
        scheduleExecuteInstructionEvent(maa->getTicksToCycles(finish_tick - curTick()));
        return true;
    } else if (force) {
        scheduleExecuteInstructionEvent(Cycles(0));
        return true;
    }
    return false;
}
void ALUUnit::executeInstruction() {
    switch (state) {
    case Status::Idle: {
        assert(my_instruction != nullptr);
        DPRINTF(MAAALU, "A[%d] %s: idling %s!\n", my_alu_id, __func__, my_instruction->print());
        DPRINTF(MAATrace, "A[%d] Start [%s]\n", my_alu_id, my_instruction->print());
        state = Status::Decode;
        [[fallthrough]];
    }
    case Status::Decode: {
        assert(my_instruction != nullptr);
        DPRINTF(MAAALU, "A[%d] %s: decoding %s!\n", my_alu_id, __func__, my_instruction->print());

        // Decoding the instruction
        my_dst_tile = my_instruction->dst1SpdID;
        my_dst_reg = my_instruction->dst1RegID;
        my_cond_tile = my_instruction->condSpdID;
        my_src1_tile = my_instruction->src1SpdID;
        my_src2_tile = my_instruction->src2SpdID;
        my_max = -1;
        my_i = 0;
        my_input_word_size = my_instruction->getWordSize(my_src1_tile);
        my_output_word_size = my_dst_tile != -1 ? my_instruction->getWordSize(my_dst_tile) : 1;
        my_input_words_per_cl = 64 / my_input_word_size;
        maa->stats.numInst++;
        if (my_instruction->opcode == Instruction::OpcodeType::ALU_SCALAR) {
            maa->stats.numInst_ALUS++;
        } else if (my_instruction->opcode == Instruction::OpcodeType::ALU_VECTOR) {
            maa->stats.numInst_ALUV++;
        } else if (my_instruction->opcode == Instruction::OpcodeType::ALU_REDUCE) {
            maa->stats.numInst_ALUR++;
            panic_if(my_dst_reg == -1, "A[%d] %s: ALU_REDUCE instruction %s has no destination register!\n", my_alu_id, __func__, my_instruction->print());
            switch (my_instruction->optype) {
            case Instruction::OPType::OR_OP:
            case Instruction::OPType::ADD_OP:
            case Instruction::OPType::SUB_OP: {
                my_red_i32 = 0;
                my_red_u32 = 0;
                my_red_i64 = 0;
                my_red_u64 = 0;
                my_red_f32 = 0;
                my_red_f64 = 0;
                break;
            }
            case Instruction::OPType::MUL_OP:
            case Instruction::OPType::DIV_OP: {
                my_red_i32 = 1;
                my_red_u32 = 1;
                my_red_i64 = 1;
                my_red_u64 = 1;
                my_red_f32 = 1;
                my_red_f64 = 1;
                break;
            }
            case Instruction::OPType::MIN_OP: {
                my_red_i32 = std::numeric_limits<int32_t>::max();
                my_red_u32 = std::numeric_limits<uint32_t>::max();
                my_red_i64 = std::numeric_limits<int64_t>::max();
                my_red_u64 = std::numeric_limits<uint64_t>::max();
                my_red_f32 = std::numeric_limits<float>::max();
                my_red_f64 = std::numeric_limits<double>::max();
                break;
            }
            case Instruction::OPType::MAX_OP: {
                my_red_i32 = std::numeric_limits<int32_t>::min();
                my_red_u32 = std::numeric_limits<uint32_t>::min();
                my_red_i64 = std::numeric_limits<int64_t>::min();
                my_red_u64 = std::numeric_limits<uint64_t>::min();
                my_red_f32 = std::numeric_limits<float>::min();
                my_red_f64 = std::numeric_limits<double>::min();
                break;
            }
            case Instruction::OPType::AND_OP: {
                my_red_i32 = 0xFFFFFFFF;
                my_red_u32 = 0xFFFFFFFF;
                my_red_i64 = 0xFFFFFFFFFFFFFFFF;
                my_red_u64 = 0xFFFFFFFFFFFFFFFF;
                break;
            }
            default:
                assert(false);
            }
        } else {
            assert(false);
        }
        (*maa->stats.ALU_NumInsts[my_alu_id])++;
        if (my_instruction->optype == Instruction::OPType::ADD_OP ||
            my_instruction->optype == Instruction::OPType::SUB_OP ||
            my_instruction->optype == Instruction::OPType::MUL_OP ||
            my_instruction->optype == Instruction::OPType::DIV_OP ||
            my_instruction->optype == Instruction::OPType::MIN_OP ||
            my_instruction->optype == Instruction::OPType::MAX_OP ||
            my_instruction->optype == Instruction::OPType::AND_OP ||
            my_instruction->optype == Instruction::OPType::OR_OP ||
            my_instruction->optype == Instruction::OPType::XOR_OP ||
            my_instruction->optype == Instruction::OPType::SHL_OP ||
            my_instruction->optype == Instruction::OPType::SHR_OP) {
            (*maa->stats.ALU_NumInstsCompute[my_alu_id])++;
        } else {
            (*maa->stats.ALU_NumInstsCompare[my_alu_id])++;
        }
        my_output_words_per_cl = 64 / my_output_word_size;
        my_SPD_read_finish_tick = curTick();
        my_SPD_write_finish_tick = curTick();
        my_ALU_finish_tick = curTick();
        my_decode_start_tick = curTick();
        my_cond_tile_ready = (my_cond_tile == -1) ? true : false;
        my_src1_tile_ready = false;
        my_src2_tile_ready = (my_instruction->opcode == Instruction::OpcodeType::ALU_SCALAR) ? true : false;

        // Setting the state of the instruction and ALU unit
        DPRINTF(MAAALU, "A[%d] %s: state set to work for request %s!\n", my_alu_id, __func__, my_instruction->print());
        my_instruction->state = Instruction::Status::Service;
        state = Status::Work;
        [[fallthrough]];
    }
    case Status::Work: {
        assert(my_instruction != nullptr);
        DPRINTF(MAAALU, "A[%d] %s: working %s!\n", my_alu_id, __func__, my_instruction->print());
        int num_spd_read_data_accesses = 0;
        int num_spd_read_cond_accesses = 0;
        int num_spd_write_accesses = 0;
        int num_alu_accesses = 0;
        // Check if any of the source tiles are ready
        // Set my_max to the size of the ready tile
        if (my_cond_tile != -1) {
            if (maa->spd->getTileStatus(my_cond_tile) == SPD::TileStatus::Finished) {
                my_cond_tile_ready = true;
                if (my_max == -1) {
                    my_max = maa->spd->getSize(my_cond_tile);
                    DPRINTF(MAAALU, "A[%d] %s: my_max = cond size (%d)!\n", my_alu_id, __func__, my_max);
                }
                panic_if(maa->spd->getSize(my_cond_tile) != my_max, "A[%d] %s: cond size (%d) != max (%d)!\n", my_alu_id, __func__, maa->spd->getSize(my_cond_tile), my_max);
            }
        }
        if (maa->spd->getTileStatus(my_src1_tile) == SPD::TileStatus::Finished) {
            my_src1_tile_ready = true;
            if (my_max == -1) {
                my_max = maa->spd->getSize(my_src1_tile);
                DPRINTF(MAAALU, "A[%d] %s: my_max = src1 size (%d)!\n", my_alu_id, __func__, my_max);
            }
            panic_if(maa->spd->getSize(my_src1_tile) != my_max, "A[%d] %s: src1 size (%d) != max (%d)!\n", my_alu_id, __func__, maa->spd->getSize(my_src1_tile), my_max);
        }
        if (my_instruction->opcode == Instruction::OpcodeType::ALU_VECTOR) {
            if (maa->spd->getTileStatus(my_src2_tile) == SPD::TileStatus::Finished) {
                my_src2_tile_ready = true;
                if (my_max == -1) {
                    my_max = maa->spd->getSize(my_src2_tile);
                    DPRINTF(MAAALU, "A[%d] %s: my_max = src2 size (%d)!\n", my_alu_id, __func__, my_max);
                }
                panic_if(maa->spd->getSize(my_src2_tile) != my_max, "A[%d] %s: src2 size (%d) != max (%d)!\n", my_alu_id, __func__, maa->spd->getSize(my_src2_tile), my_max);
            }
        }
        while (true) {
            if (my_max != -1 && my_i >= my_max) {
                if (my_cond_tile_ready == false) {
                    DPRINTF(MAAALU, "A[%d] %s: cond tile[%d] not ready, returning!\n", my_alu_id, __func__, my_cond_tile);
                    // Just a fake access to callback ALU when the condition is ready
                    maa->spd->getElementFinished(my_cond_tile, my_i, 4, (uint8_t)FuncUnitType::ALU, my_alu_id);
                    return;
                } else if (my_src1_tile_ready == false) {
                    DPRINTF(MAAALU, "A[%d] %s: src1 tile[%d] not ready, returning!\n", my_alu_id, __func__, my_src1_tile);
                    // Just a fake access to callback ALU when the src1 is ready
                    maa->spd->getElementFinished(my_src1_tile, my_i, my_input_word_size, (uint8_t)FuncUnitType::ALU, my_alu_id);
                    return;
                } else if (my_src2_tile_ready == false) {
                    DPRINTF(MAAALU, "A[%d] %s: src2 tile[%d] not ready, returning!\n", my_alu_id, __func__, my_src2_tile);
                    // Just a fake access to callback ALU when the src2 is ready
                    maa->spd->getElementFinished(my_src2_tile, my_i, my_input_word_size, (uint8_t)FuncUnitType::ALU, my_alu_id);
                    return;
                }
                DPRINTF(MAAALU, "A[%d] %s: my_i (%d) >= my_max (%d), finished!\n", my_alu_id, __func__, my_i, my_max);
                break;
            }
            bool cond_ready = my_cond_tile == -1 || maa->spd->getElementFinished(my_cond_tile, my_i, 4, (uint8_t)FuncUnitType::ALU, my_alu_id);
            bool src1_ready = cond_ready && maa->spd->getElementFinished(my_src1_tile, my_i, my_input_word_size, (uint8_t)FuncUnitType::ALU, my_alu_id);
            bool src2_ready = src1_ready && (my_instruction->opcode != Instruction::OpcodeType::ALU_VECTOR ||
                                             maa->spd->getElementFinished(my_src2_tile, my_i, my_input_word_size, (uint8_t)FuncUnitType::ALU, my_alu_id));
            if (cond_ready == false) {
                DPRINTF(MAAALU, "A[%d] %s: cond tile[%d] element[%d] not ready, returning!\n", my_alu_id, __func__, my_cond_tile, my_i);
            } else if (src1_ready == false) {
                DPRINTF(MAAALU, "A[%d] %s: src1 tile[%d] element[%d] not ready, returning!\n", my_alu_id, __func__, my_src1_tile, my_i);
            } else if (src2_ready == false) {
                DPRINTF(MAAALU, "A[%d] %s: src2 tile[%d] element[%d] not ready, returning!\n", my_alu_id, __func__, my_src2_tile, my_i);
            }
            if (cond_ready == false || src1_ready == false || src2_ready == false) {
                updateLatency(num_spd_read_data_accesses, num_spd_read_cond_accesses, num_spd_write_accesses, num_alu_accesses);
                return;
            }
            if (my_cond_tile == -1 || maa->spd->getData<uint32_t>(my_cond_tile, my_i) != 0) {
                num_alu_accesses++;
                switch (my_instruction->datatype) {
                case Instruction::DataType::UINT32_TYPE: {
                    uint32_t src1 = maa->spd->getData<uint32_t>(my_src1_tile, my_i);
                    num_spd_read_data_accesses++;
                    uint32_t src2;
                    if (my_instruction->opcode == Instruction::OpcodeType::ALU_SCALAR) {
                        src2 = maa->rf->getData<uint32_t>(my_instruction->src1RegID);
                    } else if (my_instruction->opcode == Instruction::OpcodeType::ALU_VECTOR) {
                        src2 = maa->spd->getData<uint32_t>(my_src2_tile, my_i);
                        num_spd_read_data_accesses++;
                    } else {
                        src2 = my_red_u32;
                    }
                    uint32_t result_u32_compare = 0;
                    uint32_t result_u32_compute = 0;
                    switch (my_instruction->optype) {
                    case Instruction::OPType::ADD_OP:
                        result_u32_compute = src1 + src2;
                        break;
                    case Instruction::OPType::SUB_OP:
                        result_u32_compute = src1 - src2;
                        break;
                    case Instruction::OPType::MUL_OP:
                        result_u32_compute = src1 * src2;
                        break;
                    case Instruction::OPType::DIV_OP:
                        result_u32_compute = src1 / src2;
                        break;
                    case Instruction::OPType::MIN_OP:
                        result_u32_compute = std::min(src1, src2);
                        break;
                    case Instruction::OPType::MAX_OP:
                        result_u32_compute = std::max(src1, src2);
                        break;
                    case Instruction::OPType::AND_OP:
                        result_u32_compute = src1 & src2;
                        break;
                    case Instruction::OPType::OR_OP:
                        result_u32_compute = src1 | src2;
                        break;
                    case Instruction::OPType::XOR_OP:
                        result_u32_compute = src1 ^ src2;
                        break;
                    case Instruction::OPType::SHL_OP:
                        result_u32_compute = src1 << src2;
                        break;
                    case Instruction::OPType::SHR_OP:
                        result_u32_compute = src1 >> src2;
                        break;
                    case Instruction::OPType::GT_OP:
                        result_u32_compare = src1 > src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::GTE_OP:
                        result_u32_compare = src1 >= src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::LT_OP:
                        result_u32_compare = src1 < src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::LTE_OP:
                        result_u32_compare = src1 <= src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::EQ_OP:
                        result_u32_compare = src1 == src2 ? 1 : 0;
                        break;
                    default:
                        assert(false);
                    }
                    if (my_instruction->optype == Instruction::OPType::GT_OP ||
                        my_instruction->optype == Instruction::OPType::GTE_OP ||
                        my_instruction->optype == Instruction::OPType::LT_OP ||
                        my_instruction->optype == Instruction::OPType::LTE_OP ||
                        my_instruction->optype == Instruction::OPType::EQ_OP) {
                        if (my_instruction->opcode == Instruction::OpcodeType::ALU_REDUCE) {
                            my_red_u32 = result_u32_compare;
                        } else {
                            maa->spd->setData<uint32_t>(my_dst_tile, my_i, result_u32_compare);
                            num_spd_write_accesses++;
                        }
                        (*maa->stats.ALU_NumComparedWords[my_alu_id])++;
                        if (result_u32_compare != 0) {
                            (*maa->stats.ALU_NumTakenWords[my_alu_id])++;
                        }
                    } else {
                        if (my_instruction->opcode == Instruction::OpcodeType::ALU_REDUCE) {
                            my_red_u32 = result_u32_compute;
                        } else {
                            maa->spd->setData<uint32_t>(my_dst_tile, my_i, result_u32_compute);
                            num_spd_write_accesses++;
                        }
                    }
                    break;
                }
                case Instruction::DataType::INT32_TYPE: {
                    int32_t src1 = maa->spd->getData<int32_t>(my_src1_tile, my_i);
                    num_spd_read_data_accesses++;
                    int32_t src2;
                    if (my_instruction->opcode == Instruction::OpcodeType::ALU_SCALAR) {
                        src2 = maa->rf->getData<int32_t>(my_instruction->src1RegID);
                    } else if (my_instruction->opcode == Instruction::OpcodeType::ALU_VECTOR) {
                        src2 = maa->spd->getData<int32_t>(my_src2_tile, my_i);
                        num_spd_read_data_accesses++;
                    } else {
                        src2 = my_red_i32;
                    }
                    int32_t result_i32 = 0;
                    uint32_t result_u32 = 0;
                    switch (my_instruction->optype) {
                    case Instruction::OPType::ADD_OP:
                        result_i32 = src1 + src2;
                        break;
                    case Instruction::OPType::SUB_OP:
                        result_i32 = src1 - src2;
                        break;
                    case Instruction::OPType::MUL_OP:
                        result_i32 = src1 * src2;
                        break;
                    case Instruction::OPType::DIV_OP:
                        result_i32 = src1 / src2;
                        break;
                    case Instruction::OPType::MIN_OP:
                        result_i32 = std::min(src1, src2);
                        break;
                    case Instruction::OPType::MAX_OP:
                        result_i32 = std::max(src1, src2);
                        break;
                    case Instruction::OPType::AND_OP:
                        result_i32 = src1 & src2;
                        break;
                    case Instruction::OPType::OR_OP:
                        result_i32 = src1 | src2;
                        break;
                    case Instruction::OPType::XOR_OP:
                        result_i32 = src1 ^ src2;
                        break;
                    case Instruction::OPType::SHL_OP:
                        result_i32 = src1 << src2;
                        break;
                    case Instruction::OPType::SHR_OP:
                        result_i32 = src1 >> src2;
                        break;
                    case Instruction::OPType::GT_OP:
                        result_u32 = src1 > src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::GTE_OP:
                        result_u32 = src1 >= src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::LT_OP:
                        result_u32 = src1 < src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::LTE_OP:
                        result_u32 = src1 <= src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::EQ_OP:
                        result_u32 = src1 == src2 ? 1 : 0;
                        break;
                    default:
                        assert(false);
                    }
                    if (my_instruction->optype == Instruction::OPType::GT_OP ||
                        my_instruction->optype == Instruction::OPType::GTE_OP ||
                        my_instruction->optype == Instruction::OPType::LT_OP ||
                        my_instruction->optype == Instruction::OPType::LTE_OP ||
                        my_instruction->optype == Instruction::OPType::EQ_OP) {
                        if (my_instruction->opcode == Instruction::OpcodeType::ALU_REDUCE) {
                            my_red_u32 = result_u32;
                        } else {
                            maa->spd->setData<uint32_t>(my_dst_tile, my_i, result_u32);
                            num_spd_write_accesses++;
                        }
                        (*maa->stats.ALU_NumComparedWords[my_alu_id])++;
                        if (result_u32 != 0) {
                            (*maa->stats.ALU_NumTakenWords[my_alu_id])++;
                        }
                    } else {
                        if (my_instruction->opcode == Instruction::OpcodeType::ALU_REDUCE) {
                            my_red_i32 = result_i32;
                        } else {
                            maa->spd->setData<int32_t>(my_dst_tile, my_i, result_i32);
                            num_spd_write_accesses++;
                        }
                    }
                    break;
                }
                case Instruction::DataType::FLOAT32_TYPE: {
                    float src1 = maa->spd->getData<float>(my_src1_tile, my_i);
                    num_spd_read_data_accesses++;
                    float src2;
                    if (my_instruction->opcode == Instruction::OpcodeType::ALU_SCALAR) {
                        src2 = maa->rf->getData<float>(my_instruction->src1RegID);
                    } else if (my_instruction->opcode == Instruction::OpcodeType::ALU_VECTOR) {
                        src2 = maa->spd->getData<float>(my_src2_tile, my_i);
                        num_spd_read_data_accesses++;
                    } else {
                        src2 = my_red_f32;
                    }
                    float result_f32 = 0;
                    uint32_t result_u32 = 0;
                    switch (my_instruction->optype) {
                    case Instruction::OPType::ADD_OP:
                        result_f32 = src1 + src2;
                        break;
                    case Instruction::OPType::SUB_OP:
                        result_f32 = src1 - src2;
                        break;
                    case Instruction::OPType::MUL_OP:
                        result_f32 = src1 * src2;
                        break;
                    case Instruction::OPType::DIV_OP:
                        result_f32 = src1 / src2;
                        break;
                    case Instruction::OPType::MIN_OP:
                        result_f32 = std::min(src1, src2);
                        break;
                    case Instruction::OPType::MAX_OP:
                        result_f32 = std::max(src1, src2);
                        break;
                    case Instruction::OPType::GT_OP:
                        result_u32 = src1 > src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::GTE_OP:
                        result_u32 = src1 >= src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::LT_OP:
                        result_u32 = src1 < src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::LTE_OP:
                        result_u32 = src1 <= src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::EQ_OP:
                        result_u32 = src1 == src2 ? 1 : 0;
                        break;
                    default:
                        assert(false);
                    }
                    if (my_instruction->optype == Instruction::OPType::GT_OP ||
                        my_instruction->optype == Instruction::OPType::GTE_OP ||
                        my_instruction->optype == Instruction::OPType::LT_OP ||
                        my_instruction->optype == Instruction::OPType::LTE_OP ||
                        my_instruction->optype == Instruction::OPType::EQ_OP) {
                        if (my_instruction->opcode == Instruction::OpcodeType::ALU_REDUCE) {
                            my_red_u32 = result_u32;
                        } else {
                            maa->spd->setData<uint32_t>(my_dst_tile, my_i, result_u32);
                            num_spd_write_accesses++;
                        }
                        (*maa->stats.ALU_NumComparedWords[my_alu_id])++;
                        if (result_u32 != 0) {
                            (*maa->stats.ALU_NumTakenWords[my_alu_id])++;
                        }
                    } else {
                        if (my_instruction->opcode == Instruction::OpcodeType::ALU_REDUCE) {
                            my_red_f32 = result_f32;
                        } else {
                            maa->spd->setData<float>(my_dst_tile, my_i, result_f32);
                            num_spd_write_accesses++;
                        }
                    }
                    break;
                }
                case Instruction::DataType::UINT64_TYPE: {
                    uint64_t src1 = maa->spd->getData<uint64_t>(my_src1_tile, my_i);
                    num_spd_read_data_accesses++;
                    uint64_t src2;
                    if (my_instruction->opcode == Instruction::OpcodeType::ALU_SCALAR) {
                        src2 = maa->rf->getData<uint64_t>(my_instruction->src1RegID);
                    } else if (my_instruction->opcode == Instruction::OpcodeType::ALU_VECTOR) {
                        src2 = maa->spd->getData<uint64_t>(my_src2_tile, my_i);
                        num_spd_read_data_accesses++;
                    } else {
                        src2 = my_red_u64;
                    }
                    uint64_t result_u64 = 0;
                    uint32_t result_u32 = 0;
                    switch (my_instruction->optype) {
                    case Instruction::OPType::ADD_OP:
                        result_u64 = src1 + src2;
                        break;
                    case Instruction::OPType::SUB_OP:
                        result_u64 = src1 - src2;
                        break;
                    case Instruction::OPType::MUL_OP:
                        result_u64 = src1 * src2;
                        break;
                    case Instruction::OPType::DIV_OP:
                        result_u64 = src1 / src2;
                        break;
                    case Instruction::OPType::MIN_OP:
                        result_u64 = std::min(src1, src2);
                        break;
                    case Instruction::OPType::MAX_OP:
                        result_u64 = std::max(src1, src2);
                        break;
                    case Instruction::OPType::AND_OP:
                        result_u64 = src1 & src2;
                        break;
                    case Instruction::OPType::OR_OP:
                        result_u64 = src1 | src2;
                        break;
                    case Instruction::OPType::XOR_OP:
                        result_u64 = src1 ^ src2;
                        break;
                    case Instruction::OPType::SHL_OP:
                        result_u64 = src1 << src2;
                        break;
                    case Instruction::OPType::SHR_OP:
                        result_u64 = src1 >> src2;
                        break;
                    case Instruction::OPType::GT_OP:
                        result_u32 = src1 > src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::GTE_OP:
                        result_u32 = src1 >= src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::LT_OP:
                        result_u32 = src1 < src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::LTE_OP:
                        result_u32 = src1 <= src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::EQ_OP:
                        result_u32 = src1 == src2 ? 1 : 0;
                        break;
                    default:
                        assert(false);
                    }
                    if (my_instruction->optype == Instruction::OPType::GT_OP ||
                        my_instruction->optype == Instruction::OPType::GTE_OP ||
                        my_instruction->optype == Instruction::OPType::LT_OP ||
                        my_instruction->optype == Instruction::OPType::LTE_OP ||
                        my_instruction->optype == Instruction::OPType::EQ_OP) {
                        if (my_instruction->opcode == Instruction::OpcodeType::ALU_REDUCE) {
                            my_red_u32 = result_u32;
                        } else {
                            maa->spd->setData<uint32_t>(my_dst_tile, my_i, result_u32);
                            num_spd_write_accesses++;
                        }
                        (*maa->stats.ALU_NumComparedWords[my_alu_id])++;
                        if (result_u32 != 0) {
                            (*maa->stats.ALU_NumTakenWords[my_alu_id])++;
                        }
                    } else {
                        if (my_instruction->opcode == Instruction::OpcodeType::ALU_REDUCE) {
                            my_red_u64 = result_u64;
                        } else {
                            maa->spd->setData<uint64_t>(my_dst_tile, my_i, result_u64);
                            num_spd_write_accesses++;
                        }
                    }
                    break;
                }
                case Instruction::DataType::INT64_TYPE: {
                    int64_t src1 = maa->spd->getData<int64_t>(my_src1_tile, my_i);
                    num_spd_read_data_accesses++;
                    int64_t src2;
                    if (my_instruction->opcode == Instruction::OpcodeType::ALU_SCALAR) {
                        src2 = maa->rf->getData<int64_t>(my_instruction->src1RegID);
                    } else if (my_instruction->opcode == Instruction::OpcodeType::ALU_VECTOR) {
                        src2 = maa->spd->getData<int64_t>(my_src2_tile, my_i);
                        num_spd_read_data_accesses++;
                    } else {
                        src2 = my_red_i64;
                    }
                    int64_t result_i64 = 0;
                    uint32_t result_u32 = 0;
                    switch (my_instruction->optype) {
                    case Instruction::OPType::ADD_OP:
                        result_i64 = src1 + src2;
                        break;
                    case Instruction::OPType::SUB_OP:
                        result_i64 = src1 - src2;
                        break;
                    case Instruction::OPType::MUL_OP:
                        result_i64 = src1 * src2;
                        break;
                    case Instruction::OPType::DIV_OP:
                        result_i64 = src1 / src2;
                        break;
                    case Instruction::OPType::MIN_OP:
                        result_i64 = std::min(src1, src2);
                        break;
                    case Instruction::OPType::MAX_OP:
                        result_i64 = std::max(src1, src2);
                        break;
                    case Instruction::OPType::AND_OP:
                        result_i64 = src1 & src2;
                        break;
                    case Instruction::OPType::OR_OP:
                        result_i64 = src1 | src2;
                        break;
                    case Instruction::OPType::XOR_OP:
                        result_i64 = src1 ^ src2;
                        break;
                    case Instruction::OPType::SHL_OP:
                        result_i64 = src1 << src2;
                        break;
                    case Instruction::OPType::SHR_OP:
                        result_i64 = src1 >> src2;
                        break;
                    case Instruction::OPType::GT_OP:
                        result_u32 = src1 > src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::GTE_OP:
                        result_u32 = src1 >= src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::LT_OP:
                        result_u32 = src1 < src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::LTE_OP:
                        result_u32 = src1 <= src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::EQ_OP:
                        result_u32 = src1 == src2 ? 1 : 0;
                        break;
                    default:
                        assert(false);
                    }
                    if (my_instruction->optype == Instruction::OPType::GT_OP ||
                        my_instruction->optype == Instruction::OPType::GTE_OP ||
                        my_instruction->optype == Instruction::OPType::LT_OP ||
                        my_instruction->optype == Instruction::OPType::LTE_OP ||
                        my_instruction->optype == Instruction::OPType::EQ_OP) {
                        if (my_instruction->opcode == Instruction::OpcodeType::ALU_REDUCE) {
                            my_red_u32 = result_u32;
                        } else {
                            maa->spd->setData<uint32_t>(my_dst_tile, my_i, result_u32);
                            num_spd_write_accesses++;
                        }
                        (*maa->stats.ALU_NumComparedWords[my_alu_id])++;
                        if (result_u32 != 0) {
                            (*maa->stats.ALU_NumTakenWords[my_alu_id])++;
                        }
                    } else {
                        if (my_instruction->opcode == Instruction::OpcodeType::ALU_REDUCE) {
                            my_red_i64 = result_i64;
                        } else {
                            maa->spd->setData<uint64_t>(my_dst_tile, my_i, result_i64);
                            num_spd_write_accesses++;
                        }
                    }
                    break;
                }
                case Instruction::DataType::FLOAT64_TYPE: {
                    double src1 = maa->spd->getData<double>(my_src1_tile, my_i);
                    num_spd_read_data_accesses++;
                    double src2;
                    if (my_instruction->opcode == Instruction::OpcodeType::ALU_SCALAR) {
                        src2 = maa->rf->getData<double>(my_instruction->src1RegID);
                    } else if (my_instruction->opcode == Instruction::OpcodeType::ALU_VECTOR) {
                        src2 = maa->spd->getData<double>(my_src2_tile, my_i);
                        num_spd_read_data_accesses++;
                    } else {
                        src2 = my_red_f64;
                    }
                    double result_f64 = 0;
                    uint32_t result_u32 = 0;
                    switch (my_instruction->optype) {
                    case Instruction::OPType::ADD_OP:
                        result_f64 = src1 + src2;
                        break;
                    case Instruction::OPType::SUB_OP:
                        result_f64 = src1 - src2;
                        break;
                    case Instruction::OPType::MUL_OP:
                        result_f64 = src1 * src2;
                        break;
                    case Instruction::OPType::DIV_OP:
                        result_f64 = src1 / src2;
                        break;
                    case Instruction::OPType::MIN_OP:
                        result_f64 = std::min(src1, src2);
                        break;
                    case Instruction::OPType::MAX_OP:
                        result_f64 = std::max(src1, src2);
                        break;
                    case Instruction::OPType::GT_OP:
                        result_u32 = src1 > src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::GTE_OP:
                        result_u32 = src1 >= src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::LT_OP:
                        result_u32 = src1 < src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::LTE_OP:
                        result_u32 = src1 <= src2 ? 1 : 0;
                        break;
                    case Instruction::OPType::EQ_OP:
                        result_u32 = src1 == src2 ? 1 : 0;
                        break;
                    default:
                        assert(false);
                    }
                    if (my_instruction->optype == Instruction::OPType::GT_OP ||
                        my_instruction->optype == Instruction::OPType::GTE_OP ||
                        my_instruction->optype == Instruction::OPType::LT_OP ||
                        my_instruction->optype == Instruction::OPType::LTE_OP ||
                        my_instruction->optype == Instruction::OPType::EQ_OP) {
                        if (my_instruction->opcode == Instruction::OpcodeType::ALU_REDUCE) {
                            my_red_u32 = result_u32;
                        } else {
                            maa->spd->setData<uint32_t>(my_dst_tile, my_i, result_u32);
                            num_spd_write_accesses++;
                        }
                        (*maa->stats.ALU_NumComparedWords[my_alu_id])++;
                        if (result_u32 != 0) {
                            (*maa->stats.ALU_NumTakenWords[my_alu_id])++;
                        }
                    } else {
                        if (my_instruction->opcode == Instruction::OpcodeType::ALU_REDUCE) {
                            my_red_f64 = result_f64;
                        } else {
                            maa->spd->setData<double>(my_dst_tile, my_i, result_f64);
                            num_spd_write_accesses++;
                        }
                    }
                    break;
                }
                default:
                    assert(false);
                }
            } else if (my_dst_tile != -1) {
                DPRINTF(MAAALU, "A[%d] %s: SPD[%d][%d] = %u (cond not taken)\n", my_alu_id, __func__, my_dst_tile, my_i, 0);
                if (my_instruction->optype == Instruction::OPType::GT_OP ||
                    my_instruction->optype == Instruction::OPType::GTE_OP ||
                    my_instruction->optype == Instruction::OPType::LT_OP ||
                    my_instruction->optype == Instruction::OPType::LTE_OP ||
                    my_instruction->optype == Instruction::OPType::EQ_OP) {
                    maa->spd->setData<uint32_t>(my_dst_tile, my_i, 0);
                } else {
                    switch (my_instruction->datatype) {
                    case Instruction::DataType::UINT32_TYPE: {
                        maa->spd->setData<uint32_t>(my_dst_tile, my_i, 0);
                        break;
                    }
                    case Instruction::DataType::INT32_TYPE: {
                        maa->spd->setData<int32_t>(my_dst_tile, my_i, 0);
                        break;
                    }
                    case Instruction::DataType::FLOAT32_TYPE: {
                        maa->spd->setData<float>(my_dst_tile, my_i, 0);
                        break;
                    }
                    case Instruction::DataType::UINT64_TYPE: {
                        maa->spd->setData<uint64_t>(my_dst_tile, my_i, 0);
                        break;
                    }
                    case Instruction::DataType::INT64_TYPE: {
                        maa->spd->setData<int64_t>(my_dst_tile, my_i, 0);
                        break;
                    }
                    case Instruction::DataType::FLOAT64_TYPE: {
                        maa->spd->setData<double>(my_dst_tile, my_i, 0);
                        break;
                    }
                    default:
                        assert(false);
                    }
                }
            }
            my_i++;
        }
        updateLatency(num_spd_read_data_accesses, num_spd_read_cond_accesses, num_spd_write_accesses, num_alu_accesses);
        DPRINTF(MAAALU, "A[%d] %s: setting state to finish for request %s!\n", my_alu_id, __func__, my_instruction->print());
        state = Status::Finish;
        scheduleNextExecution(true);
        break;
    }
    case Status::Finish: {
        DPRINTF(MAAALU, "A[%d] %s: finishing %s!\n", my_alu_id, __func__, my_instruction->print());
        DPRINTF(MAATrace, "A[%d] End [%s]\n", my_alu_id, my_instruction->print());
        my_instruction->state = Instruction::Status::Finish;
        state = Status::Idle;
        panic_if(my_max != -1 && my_i != my_max, "A[%d] %s: my_i (%d) != my_max (%d)!\n", my_alu_id, __func__, my_i, my_max);
        panic_if(my_cond_tile_ready == false, "A[%d] %s: cond tile[%d] not ready!\n", my_alu_id, __func__, my_cond_tile);
        panic_if(my_src1_tile_ready == false, "A[%d] %s: src1 tile[%d] not ready!\n", my_alu_id, __func__, my_src1_tile);
        panic_if(my_src2_tile_ready == false, "A[%d] %s: src2 tile[%d] not ready!\n", my_alu_id, __func__, my_src2_tile);
        if (my_dst_tile != -1) {
            maa->spd->setSize(my_dst_tile, my_i);
        } else {
            panic_if(my_instruction->opcode != Instruction::OpcodeType::ALU_REDUCE, "A[%d] %s: ALU_VECTOR/ALU_SCALAR without dst_tile!\n", my_alu_id, __func__);
            if (my_instruction->optype == Instruction::OPType::GT_OP ||
                my_instruction->optype == Instruction::OPType::GTE_OP ||
                my_instruction->optype == Instruction::OPType::LT_OP ||
                my_instruction->optype == Instruction::OPType::LTE_OP ||
                my_instruction->optype == Instruction::OPType::EQ_OP ||
                my_instruction->datatype == Instruction::DataType::UINT32_TYPE) {
                maa->rf->setData<uint32_t>(my_instruction->dst1RegID, my_red_u32);
            } else if (my_instruction->datatype == Instruction::DataType::INT32_TYPE) {
                maa->rf->setData<int32_t>(my_instruction->dst1RegID, my_red_i32);
            } else if (my_instruction->datatype == Instruction::DataType::FLOAT32_TYPE) {
                maa->rf->setData<float>(my_instruction->dst1RegID, my_red_f32);
            } else if (my_instruction->datatype == Instruction::DataType::UINT64_TYPE) {
                maa->rf->setData<uint64_t>(my_instruction->dst1RegID, my_red_u64);
            } else if (my_instruction->datatype == Instruction::DataType::INT64_TYPE) {
                maa->rf->setData<int64_t>(my_instruction->dst1RegID, my_red_i64);
            } else if (my_instruction->datatype == Instruction::DataType::FLOAT64_TYPE) {
                maa->rf->setData<double>(my_instruction->dst1RegID, my_red_f64);
            } else {
                assert(false);
            }
        }
        maa->finishInstructionCompute(my_instruction);
        Cycles total_cycles = maa->getTicksToCycles(curTick() - my_decode_start_tick);
        maa->stats.cycles += total_cycles;
        if (my_instruction->opcode == Instruction::OpcodeType::ALU_SCALAR) {
            maa->stats.cycles_ALUS += total_cycles;
        } else if (my_instruction->opcode == Instruction::OpcodeType::ALU_VECTOR) {
            maa->stats.cycles_ALUV += total_cycles;
        } else if (my_instruction->opcode == Instruction::OpcodeType::ALU_REDUCE) {
            maa->stats.cycles_ALUR += total_cycles;
        } else {
            assert(false);
        }
        my_instruction = nullptr;
        break;
    }
    default:
        assert(false);
    }
}
void ALUUnit::setInstruction(Instruction *_instruction) {
    assert(my_instruction == nullptr);
    my_instruction = _instruction;
}
void ALUUnit::scheduleExecuteInstructionEvent(int latency) {
    DPRINTF(MAAALU, "A[%d] %s: scheduling execute for the ALU Unit in the next %d cycles!\n", my_alu_id, __func__, latency);
    Tick new_when = maa->getClockEdge(Cycles(latency));
    panic_if(executeInstructionEvent.scheduled(), "Event already scheduled!\n");
    maa->schedule(executeInstructionEvent, new_when);
    // if (!executeInstructionEvent.scheduled()) {
    //     maa->schedule(executeInstructionEvent, new_when);
    // } else {
    //     Tick old_when = executeInstructionEvent.when();
    //     if (new_when < old_when)
    //         maa->reschedule(executeInstructionEvent, new_when);
    // }
}
} // namespace gem5