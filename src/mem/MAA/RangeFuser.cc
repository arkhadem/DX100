#include "mem/MAA/RangeFuser.hh"
#include "base/types.hh"
#include "mem/MAA/MAA.hh"
#include "mem/MAA/IF.hh"
#include "mem/MAA/SPD.hh"
#include "base/trace.hh"
#include "debug/MAARangeFuser.hh"
#include "debug/MAATrace.hh"
#include <cassert>

#ifndef TRACING_ON
#define TRACING_ON 1
#endif

namespace gem5 {
///////////////
//
// RANGE ACCESS UNIT
//
///////////////
RangeFuserUnit::RangeFuserUnit()
    : executeInstructionEvent([this] { executeInstruction(); }, name()) {
    my_instruction = nullptr;
}
void RangeFuserUnit::allocate(unsigned int _num_tile_elements, MAA *_maa, int _my_range_id) {
    state = Status::Idle;
    num_tile_elements = _num_tile_elements;
    maa = _maa;
    my_range_id = _my_range_id;
    my_instruction = nullptr;
}
void RangeFuserUnit::updateLatency(int num_spd_read_accesses,
                                   int num_spd_write_accesses,
                                   int num_compute_accesses) {
    if (num_spd_read_accesses != 0) {
        // 4Byte conditions and indices -- 16 bytes per SPD access
        Cycles get_data_latency = maa->spd->getDataLatency(getCeiling(num_spd_read_accesses, 16));
        my_SPD_read_finish_tick = maa->getClockEdge(get_data_latency);
        (*maa->stats.RNG_CyclesSPDReadAccess[my_range_id]) += get_data_latency;
    }
    if (num_spd_write_accesses != 0) {
        // 4Byte indices -- 64/X bytes per SPD access
        int spd_write_accesses = getCeiling(num_spd_write_accesses, 16);
        Cycles set_data_latency = maa->spd->setDataLatency(my_dst_i_tile, spd_write_accesses);
        set_data_latency = maa->spd->setDataLatency(my_dst_j_tile, spd_write_accesses);
        my_SPD_write_finish_tick = maa->getClockEdge(set_data_latency);
        (*maa->stats.RNG_CyclesSPDWriteAccess[my_range_id]) += set_data_latency;
    }
    if (num_compute_accesses != 0) {
        // RANGE operations
        Cycles compute_latency = Cycles(num_compute_accesses);
        if (my_compute_finish_tick < curTick())
            my_compute_finish_tick = maa->getClockEdge(compute_latency);
        else
            my_compute_finish_tick += maa->getCyclesToTicks(compute_latency);
        (*maa->stats.RNG_CyclesCompute[my_range_id]) += compute_latency;
    }
}
bool RangeFuserUnit::scheduleNextExecution(bool force) {
    Tick finish_tick = std::max(std::max(my_SPD_read_finish_tick, my_SPD_write_finish_tick), my_compute_finish_tick);
    if (curTick() < finish_tick) {
        scheduleExecuteInstructionEvent(maa->getTicksToCycles(finish_tick - curTick()));
        return true;
    } else if (force) {
        scheduleExecuteInstructionEvent(Cycles(0));
        return true;
    }
    return false;
}
void RangeFuserUnit::executeInstruction() {
    switch (state) {
    case Status::Idle: {
        assert(my_instruction != nullptr);
        DPRINTF(MAARangeFuser, "R[%d] %s: idling %s!\n", my_range_id, __func__, my_instruction->print());
        DPRINTF(MAATrace, "R[%d] Start [%s]\n", my_range_id, my_instruction->print());
        state = Status::Decode;
        [[fallthrough]];
    }
    case Status::Decode: {
        assert(my_instruction != nullptr);
        DPRINTF(MAARangeFuser, "R[%d] %s: decoding %s!\n", my_range_id, __func__, my_instruction->print());

        // Decoding the instruction
        my_dst_i_tile = my_instruction->dst1SpdID;
        my_dst_j_tile = my_instruction->dst2SpdID;
        my_cond_tile = my_instruction->condSpdID;
        my_min_tile = my_instruction->src1SpdID;
        my_max_tile = my_instruction->src2SpdID;
        my_last_i = maa->rf->getData<int>(my_instruction->dst1RegID);
        my_last_j = maa->rf->getData<int>(my_instruction->dst2RegID);
        my_stride = maa->rf->getData<int>(my_instruction->src1RegID);
        my_max_i = -1;
        my_idx_j = 0;
        DPRINTF(MAARangeFuser, "R[%d] %s: my_last_i: %d, my_last_j: %d, my_stride: %d\n",
                my_range_id, __func__, my_last_i, my_last_j, my_stride);
        maa->stats.numInst_RANGE++;
        (*maa->stats.RNG_NumInsts[my_range_id])++;
        maa->stats.numInst++;
        my_SPD_read_finish_tick = curTick();
        my_SPD_write_finish_tick = curTick();
        my_compute_finish_tick = curTick();
        my_decode_start_tick = curTick();
        my_cond_tile_ready = (my_cond_tile == -1) ? true : false;
        my_min_tile_ready = false;
        my_max_tile_ready = false;

        // Setting the state of the instruction and RANGE unit
        DPRINTF(MAARangeFuser, "R[%d] %s: state set to work for request %s!\n", my_range_id, __func__, my_instruction->print());
        my_instruction->state = Instruction::Status::Service;
        state = Status::Work;
        [[fallthrough]];
    }
    case Status::Work: {
        assert(my_instruction != nullptr);
        DPRINTF(MAARangeFuser, "R[%d] %s: working %s!\n", my_range_id, __func__, my_instruction->print());
        int num_spd_read_accesses = 0;
        int num_spd_write_accesses = 0;
        int num_computed_words = 0;
        if (my_cond_tile != -1) {
            if (maa->spd->getTileStatus(my_cond_tile) == SPD::TileStatus::Finished) {
                my_cond_tile_ready = true;
                if (my_max_i == -1) {
                    my_max_i = maa->spd->getSize(my_cond_tile);
                    DPRINTF(MAARangeFuser, "R[%d] %s: my_max_i = cond size (%d)!\n", my_range_id, __func__, my_max_i);
                }
                panic_if(maa->spd->getSize(my_cond_tile) != my_max_i, "R[%d] %s: cond size (%d) != max (%d)!\n", my_range_id, __func__, maa->spd->getSize(my_cond_tile), my_max_i);
            }
        }
        if (maa->spd->getTileStatus(my_min_tile) == SPD::TileStatus::Finished) {
            my_min_tile_ready = true;
            if (my_max_i == -1) {
                my_max_i = maa->spd->getSize(my_min_tile);
                DPRINTF(MAARangeFuser, "R[%d] %s: my_max_i = min size (%d)!\n", my_range_id, __func__, my_max_i);
            }
            panic_if(maa->spd->getSize(my_min_tile) != my_max_i, "R[%d] %s: min size (%d) != max (%d)!\n", my_range_id, __func__, maa->spd->getSize(my_min_tile), my_max_i);
        }
        if (maa->spd->getTileStatus(my_max_tile) == SPD::TileStatus::Finished) {
            my_max_tile_ready = true;
            if (my_max_i == -1) {
                my_max_i = maa->spd->getSize(my_max_tile);
                DPRINTF(MAARangeFuser, "R[%d] %s: my_max_i = max size (%d)!\n", my_range_id, __func__, my_max_i);
            }
            panic_if(maa->spd->getSize(my_max_tile) != my_max_i, "R[%d] %s: max size (%d) != max (%d)!\n", my_range_id, __func__, maa->spd->getSize(my_max_tile), my_max_i);
        }
        while (true) {
            if (my_max_i != -1 && my_last_i >= my_max_i) {
                if (my_cond_tile_ready == false) {
                    DPRINTF(MAARangeFuser, "R[%d] %s: cond tile[%d] not ready, returning!\n", my_range_id, __func__, my_cond_tile);
                    // Just a fake access to callback RANGE when the condition is ready
                    maa->spd->getElementFinished(my_cond_tile, my_last_i, 4, (uint8_t)FuncUnitType::RANGE, my_range_id);
                    return;
                } else if (my_min_tile_ready == false) {
                    DPRINTF(MAARangeFuser, "R[%d] %s: min tile[%d] not ready, returning!\n", my_range_id, __func__, my_min_tile);
                    // Just a fake access to callback RANGE when the min is ready
                    maa->spd->getElementFinished(my_min_tile, my_last_i, 4, (uint8_t)FuncUnitType::RANGE, my_range_id);
                    return;
                } else if (my_max_tile_ready == false) {
                    DPRINTF(MAARangeFuser, "R[%d] %s: max tile[%d] not ready, returning!\n", my_range_id, __func__, my_max_tile);
                    // Just a fake access to callback RANGE when the max is ready
                    maa->spd->getElementFinished(my_max_tile, my_last_i, 4, (uint8_t)FuncUnitType::RANGE, my_range_id);
                    return;
                }
                DPRINTF(MAARangeFuser, "R[%d] %s: my_last_i (%d) >= my_max_i (%d), finished!\n", my_range_id, __func__, my_last_i, my_max_i);
                break;
            }
            if (my_idx_j >= num_tile_elements) {
                DPRINTF(MAARangeFuser, "R[%d] %s: my_idx_j (%d) >= num_tile_elements (%d), finished!\n", my_range_id, __func__, my_idx_j, num_tile_elements);
                break;
            }
            // if (my_last_i >= num_tile_elements) {
            //     DPRINTF(MAARangeFuser, "R[%d] %s: my_last_i (%d) >= num_tile_elements (%d), finished!\n", __func__, my_last_i, num_tile_elements);
            //     break;
            // }
            bool cond_ready = my_cond_tile == -1 || maa->spd->getElementFinished(my_cond_tile, my_last_i, 4, (uint8_t)FuncUnitType::RANGE, my_range_id);
            bool min_ready = cond_ready && maa->spd->getElementFinished(my_min_tile, my_last_i, 4, (uint8_t)FuncUnitType::RANGE, my_range_id);
            bool max_ready = min_ready && maa->spd->getElementFinished(my_max_tile, my_last_i, 4, (uint8_t)FuncUnitType::RANGE, my_range_id);
            if (cond_ready == false) {
                DPRINTF(MAARangeFuser, "R[%d] %s: cond tile[%d] element[%d] not ready, returning!\n", my_range_id, __func__, my_cond_tile, my_last_i);
            } else if (min_ready == false) {
                DPRINTF(MAARangeFuser, "R[%d] %s: min tile[%d] element[%d] not ready, returning!\n", my_range_id, __func__, my_min_tile, my_last_i);
            } else if (max_ready == false) {
                DPRINTF(MAARangeFuser, "R[%d] %s: max tile[%d] element[%d] not ready, returning!\n", my_range_id, __func__, my_max_tile, my_last_i);
            }
            if (cond_ready == false || min_ready == false || max_ready == false) {
                updateLatency(num_spd_read_accesses, num_spd_write_accesses, num_computed_words);
                return;
            }
            if (my_cond_tile != -1) {
                num_spd_read_accesses++;
            }
            if (my_cond_tile == -1 || maa->spd->getData<uint32_t>(my_cond_tile, my_last_i) != 0) {
                if (my_last_j == -1) {
                    my_last_j = maa->spd->getData<uint32_t>(my_min_tile, my_last_i);
                    num_spd_read_accesses++;
                }
                uint32_t my_min_j = maa->spd->getData<uint32_t>(my_min_tile, my_last_i);
                uint32_t my_max_j = maa->spd->getData<uint32_t>(my_max_tile, my_last_i);
                num_spd_read_accesses++;
                num_computed_words++;
                for (; my_last_j < my_max_j && my_idx_j < num_tile_elements; my_last_j += my_stride, my_idx_j++) {
                    maa->spd->setData(my_dst_i_tile, my_idx_j, my_last_i);
                    maa->spd->setData(my_dst_j_tile, my_idx_j, my_last_j);
                    num_spd_write_accesses++;
                    DPRINTF(MAARangeFuser, "R[%d] %s: [%d-%d-%d][%d-%d-%d] inserted!\n", my_range_id, __func__, 0, my_last_i, my_max_i, my_min_j, my_last_j, my_max_j);
                }
                if (my_last_j >= my_max_j) {
                    my_last_j = -1;
                } else if (my_idx_j == num_tile_elements) {
                    break;
                }
            }
            my_last_i++;
        }
        // We have generated a tile of i and j values successfully
        // We don't have to wait for the condition, min, and max tiles to be ready
        my_cond_tile_ready = true;
        my_min_tile_ready = true;
        my_max_tile_ready = true;
        updateLatency(num_spd_read_accesses, num_spd_write_accesses, num_computed_words);
        DPRINTF(MAARangeFuser, "R[%d] %s: setting state to finish for request %s!\n", my_range_id, __func__, my_instruction->print());
        state = Status::Finish;
        scheduleNextExecution(true);
        break;
    }
    case Status::Finish: {
        DPRINTF(MAARangeFuser, "R[%d] %s: finishing %s!\n", my_range_id, __func__, my_instruction->print());
        DPRINTF(MAATrace, "R[%d] End [%s]\n", my_range_id, my_instruction->print());
        panic_if(my_cond_tile_ready == false, "R[%d] %s: cond tile[%d] not ready!\n", my_range_id, __func__, my_cond_tile);
        panic_if(my_min_tile_ready == false, "R[%d] %s: min tile[%d] not ready!\n", my_range_id, __func__, my_min_tile);
        panic_if(my_max_tile_ready == false, "R[%d] %s: max tile[%d] not ready!\n", my_range_id, __func__, my_max_tile);
        my_instruction->state = Instruction::Status::Finish;
        state = Status::Idle;
        maa->rf->setData<int>(my_instruction->dst1RegID, my_last_i);
        maa->rf->setData<int>(my_instruction->dst2RegID, my_last_j);
        int tile_size = my_idx_j; // my_idx_j == -1 ? num_tile_elements : my_idx_j;
        maa->spd->setSize(my_dst_i_tile, tile_size);
        maa->spd->setSize(my_dst_j_tile, tile_size);
        maa->finishInstructionCompute(my_instruction);
        Cycles total_cycles = maa->getTicksToCycles(curTick() - my_decode_start_tick);
        maa->stats.cycles_RANGE += total_cycles;
        maa->stats.cycles += total_cycles;
        DPRINTF(MAARangeFuser, "R[%d] %s: my_last_i: %d [REG %d], my_last_j: %d [REG %d], my_idx_j: %d, tile size: %d\n",
                my_range_id, __func__,
                my_last_i, my_instruction->dst1RegID,
                my_last_j, my_instruction->dst2RegID,
                my_idx_j, tile_size);
        my_instruction = nullptr;
        break;
    }
    default:
        assert(false);
    }
}
void RangeFuserUnit::setInstruction(Instruction *_instruction) {
    assert(my_instruction == nullptr);
    my_instruction = _instruction;
}
void RangeFuserUnit::scheduleExecuteInstructionEvent(int latency) {
    DPRINTF(MAARangeFuser, "R[%d] %s: scheduling execute for the RangeFuser Unit in the next %d cycles!\n", my_range_id, __func__, latency);
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