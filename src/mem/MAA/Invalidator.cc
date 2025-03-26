#include "mem/MAA/Invalidator.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "mem/MAA/MAA.hh"
#include "mem/MAA/SPD.hh"
#include "debug/MAAInvalidator.hh"
#include <cassert>

#ifndef TRACING_ON
#define TRACING_ON 1
#endif

namespace gem5 {
Invalidator::Invalidator()
    : executeInstructionEvent([this] { executeInstruction(); }, name()),
      transientInstructionEvent([this] { transientInstruction(); }, name()) {
    cl_status = nullptr;
    my_instruction = nullptr;
    rg_status = nullptr;
}
Invalidator::~Invalidator() {
    if (cl_status != nullptr)
        delete[] cl_status;
    if (rg_status != nullptr) {
        for (int i = 0; i < num_maas; i++) {
            if (rg_status[i] != nullptr)
                delete[] rg_status[i];
        }
        delete[] rg_status;
    }
}
void Invalidator::allocate(int _num_maas,
                           int _num_tiles,
                           int _num_tile_elements,
                           Addr _base_addr,
                           MAA *_maa) {
    num_maas = _num_maas;
    num_tiles = _num_tiles;
    num_tile_elements = _num_tile_elements;
    my_base_addr = _base_addr;
    maa = _maa;
    total_cls = num_tiles * num_tile_elements * sizeof(uint32_t) / 64;
    cl_status = new CLStatus[total_cls];
    for (int i = 0; i < total_cls; i++) {
        cl_status[i] = CLStatus::Uncached;
    }
    rg_status = new RGStatus *[num_maas];
    for (int i = 0; i < num_maas; i++) {
        rg_status[i] = new RGStatus[num_tiles];
        for (int j = 0; j < num_tiles; j++) {
            rg_status[i][j] = RGStatus::Invalid;
        }
    }
    my_instruction = nullptr;
    state = Status::Idle;
}
bool Invalidator::getAddrRegionPermit(Instruction *instruction) {
    int8_t region_id = instruction->addrRangeID;
    int maa_id = instruction->maa_id;
    if (instruction->accessType == Instruction::AccessType::COMPUTE) {
        return true;
    } else if (instruction->accessType == Instruction::AccessType::READ) {
        // We need the shared state
        switch (rg_status[maa_id][region_id]) {
        case RGStatus::Invalid: {
            // Make sure no other MAA is write waiting for region (TransientModified) or hasn't used it (UnusedModified), or using it (UsingModified)
            for (int i = 0; i < num_maas; i++) {
                if (rg_status[i][region_id] == RGStatus::TransientModified ||
                    rg_status[i][region_id] == RGStatus::UnusedModified ||
                    rg_status[i][region_id] == RGStatus::UsingModified) {
                    DPRINTF(MAAInvalidator, "Region[%d][%d] cannot be READ permitted for instruction %s because Region[%d][%d] is in %s state!\n", maa_id, region_id, instruction->print(), i, region_id, rg_status_names[(uint8_t)(rg_status[i][region_id])]);
                    return false;
                }
            }
            // If any core has used the region in modified state, we switched it to the shared state
            for (int i = 0; i < num_maas; i++) {
                if (rg_status[i][region_id] == RGStatus::UsedModified) {
                    rg_status[i][region_id] = RGStatus::UsedShared;
                    DPRINTF(MAAInvalidator, "Region[%d][%d] changed to UsedShared because of permitting READ for instruction %s!\n", i, region_id, instruction->print());
                }
            }
            // We move to the transient shared state, and wait for 100 cycles to move to unused shared state
            rg_status[maa_id][region_id] = RGStatus::TransientShared;
            DPRINTF(MAAInvalidator, "Region[%d][%d] changed to TransientShared because of permitting READ for instruction %s!\n", maa_id, region_id, instruction->print());
            panic_if(std::find(transientInstructions.begin(), transientInstructions.end(), instruction) != transientInstructions.end(), "Instruction %s already in transientInstructions!\n", instruction->print());
            transientInstructions.push_back(instruction);
            transientTicks.push_back(maa->getClockEdge(Cycles(100)));
            scheduleTransientInstructionEvent(100);
            // Meaning that the state is not granted yet
            return false;
        }
        case RGStatus::TransientShared: {
            // It's possible that we have 2 ready read instructions in a MAA instance to the same memory region.
            // We don't need the following assertion:
            // panic_if(std::find(transientInstructions.begin(), transientInstructions.end(), instruction) == transientInstructions.end(), "Instruction %s not in transientInstructions!\n", instruction->print());
            // Meaning that the state is not granted yet
            DPRINTF(MAAInvalidator, "Region[%d][%d] cannot be READ permitted for instruction %s because it is still in TransientShared state!\n", maa_id, region_id, instruction->print());
            return false;
        }
        case RGStatus::UnusedShared:
        case RGStatus::UsedShared: {
            // We move to the using shared state
            rg_status[maa_id][region_id] = RGStatus::UsingShared;
            DPRINTF(MAAInvalidator, "Region[%d][%d] changed to UsingShared because of permitting READ for instruction %s!\n", maa_id, region_id, instruction->print());
            // Meaning that the state is granted
            return true;
        }
        // It's possible that we have ready SLD and ILD instructions. But we can't handle 2 simulatenous read instructions to the same memory region.
        // This is because when one is finished, we need to change the state to UsedShared, but the other one is still in UsingShared state.
        case RGStatus::UsingShared: {
            DPRINTF(MAAInvalidator, "Region[%d][%d] cannot be READ permitted for instruction %s because another read is in UsingShared state!\n", maa_id, region_id, instruction->print());
            return false;
        }
        // The following 3 mean that there are 2 ready instructions that need to access read and write at the same time
        // We cannot allow it
        case RGStatus::TransientModified:
        case RGStatus::UnusedModified:
        case RGStatus::UsingModified: {
            DPRINTF(MAAInvalidator, "Region[%d][%d] cannot be READ permitted for instruction %s because Region[%d][%d] is requested by another WRITE in %s state!\n", maa_id, region_id, instruction->print(), maa_id, region_id, rg_status_names[(uint8_t)(rg_status[maa_id][region_id])]);
            return false;
        }
        case RGStatus::UsedModified: {
            // This means that there have been a write which is completed, we keep the modified state
            rg_status[maa_id][region_id] = RGStatus::UsingModified;
            DPRINTF(MAAInvalidator, "Region[%d][%d] changed to UsingModified because of permitting READ for instruction %s!\n", maa_id, region_id, instruction->print());
            return true;
        }
        case RGStatus::MAX: {
            panic_if(true, "Instruction %s is in MAX state!\n", instruction->print());
            return false;
        }
        }
    } else if (instruction->accessType == Instruction::AccessType::WRITE) {
        // We need the shared state
        switch (rg_status[maa_id][region_id]) {
        case RGStatus::Invalid:
        case RGStatus::UsedShared: {
            // Make sure no other MAA is read or write waiting for region (TransientModified) or hasn't used it (UnusedModified), or using it (UsingModified)
            for (int i = 0; i < num_maas; i++) {
                if (rg_status[i][region_id] == RGStatus::TransientModified ||
                    rg_status[i][region_id] == RGStatus::UnusedModified ||
                    rg_status[i][region_id] == RGStatus::UsingModified ||
                    rg_status[i][region_id] == RGStatus::TransientShared ||
                    rg_status[i][region_id] == RGStatus::UnusedShared ||
                    rg_status[i][region_id] == RGStatus::UsingShared) {
                    DPRINTF(MAAInvalidator, "Region[%d][%d] cannot be WRITE permitted for instruction %s because Region[%d][%d] is in %s state!\n", maa_id, region_id, instruction->print(), i, region_id, rg_status_names[(uint8_t)(rg_status[i][region_id])]);
                    return false;
                }
            }
            // If any core has used the region in shared or modified state, we switched it to the invalid state
            for (int i = 0; i < num_maas; i++) {
                if (rg_status[i][region_id] == RGStatus::UsedModified || rg_status[i][region_id] == RGStatus::UsedShared) {
                    rg_status[i][region_id] = RGStatus::Invalid;
                    DPRINTF(MAAInvalidator, "Region[%d][%d] changed to Invalid because of permitting WRITE for instruction %s!\n", i, region_id, instruction->print());
                }
            }
            // We move to the transient modifed state, and wait for 100 cycles to move to unused modifed state
            rg_status[maa_id][region_id] = RGStatus::TransientModified;
            DPRINTF(MAAInvalidator, "Region[%d][%d] changed to TransientModified because of permitting WRITE for instruction %s!\n", maa_id, region_id, instruction->print());
            panic_if(std::find(transientInstructions.begin(), transientInstructions.end(), instruction) != transientInstructions.end(), "Instruction %s already in transientInstructions!\n", instruction->print());
            transientInstructions.push_back(instruction);
            transientTicks.push_back(maa->getClockEdge(Cycles(100)));
            scheduleTransientInstructionEvent(100);
            // Meaning that the state is not granted yet
            return false;
        }
        // There could be 2 ready RMW instructions in a MAA instance to the same memory region, we cannot allow it.
        case RGStatus::UsingModified: {
            DPRINTF(MAAInvalidator, "Region[%d][%d] cannot be WRITE permitted for instruction %s because it is in UsingModified state for another WRITE instruction!\n", maa_id, region_id, instruction->print());
            return false;
        }
        // The following 3 mean that there are 2 ready instructions that need to access read and write at the same time, like SLD and IST.
        case RGStatus::TransientShared:
        case RGStatus::UnusedShared:
        case RGStatus::UsingShared: {
            DPRINTF(MAAInvalidator, "Region[%d][%d] cannot be WRITE permitted for instruction %s because Region[%d][%d] is requested by another READ in %s state!\n", maa_id, region_id, instruction->print(), maa_id, region_id, rg_status_names[(uint8_t)(rg_status[maa_id][region_id])]);
            return false;
        }
        case RGStatus::TransientModified: {
            // It's possible that we have 2 ready RMW instructions in a MAA instance to the same memory region, like two stores.
            // We don't need the following assertion:
            // panic_if(std::find(transientInstructions.begin(), transientInstructions.end(), instruction) == transientInstructions.end(), "Instruction %s not in transientInstructions!\n", instruction->print());
            DPRINTF(MAAInvalidator, "Region[%d][%d] cannot be WRITE permitted for instruction %s because it is still in TransientModified state!\n", maa_id, region_id, instruction->print());
            // Meaning that the state is not granted yet
            return false;
        }
        case RGStatus::UnusedModified:
        case RGStatus::UsedModified: {
            // We move to the using modified state
            rg_status[maa_id][region_id] = RGStatus::UsingModified;
            DPRINTF(MAAInvalidator, "Region[%d][%d] changed to UsingModified because of permitting WRITE for instruction %s!\n", maa_id, region_id, instruction->print());
            // Meaning that the state is granted
            return true;
        }
        case RGStatus::MAX: {
            panic_if(true, "Instruction %s is in MAX state!\n", instruction->print());
            return false;
        }
        }
    } else {
        panic_if(true, "Instruction %s has MAX type!\n", instruction->print());
        return false;
    }
    panic_if(true, "Instruction %s is not in any state!\n", instruction->print());
    return false;
}
void Invalidator::transientInstruction() {
    auto instruction_it = transientInstructions.begin();
    auto tick_it = transientTicks.begin();
    bool packet_remaining = false;
    bool transient_happenned = false;
    Tick tick_remaining = 0;
    while (instruction_it != transientInstructions.end() && tick_it != transientTicks.end()) {
        Instruction *instruction = *instruction_it;
        Tick tick = *tick_it;
        if (tick > curTick()) {
            packet_remaining = true;
            tick_remaining = tick - curTick();
            break;
        }
        switch (rg_status[instruction->maa_id][instruction->addrRangeID]) {
        case RGStatus::TransientShared: {
            panic_if(instruction->accessType != Instruction::AccessType::READ, "Instruction %s is in TransientShared state but not READ!\n", instruction->print());
            rg_status[instruction->maa_id][instruction->addrRangeID] = RGStatus::UnusedShared;
            DPRINTF(MAAInvalidator, "Region[%d][%d] changed to UnusedShared because of permitting READ for instruction %s!\n", instruction->maa_id, instruction->addrRangeID, instruction->print());
            break;
        }
        case RGStatus::TransientModified: {
            panic_if(instruction->accessType != Instruction::AccessType::WRITE, "Instruction %s is in TransientModified state but not WRITE!\n", instruction->print());
            rg_status[instruction->maa_id][instruction->addrRangeID] = RGStatus::UnusedModified;
            DPRINTF(MAAInvalidator, "Region[%d][%d] changed to UnusedModified because of permitting WRITE for instruction %s!\n", instruction->maa_id, instruction->addrRangeID, instruction->print());
            break;
        }
        default: {
            panic_if(true, "Instruction %s is not in transient state: %s!\n", instruction->print(), rg_status_names[(uint8_t)(rg_status[instruction->maa_id][instruction->addrRangeID])]);
        }
        }
        transient_happenned = true;
        instruction_it = transientInstructions.erase(instruction_it);
        tick_it = transientTicks.erase(tick_it);
    }
    if (packet_remaining) {
        scheduleTransientInstructionEvent(maa->getTicksToCycles(tick_remaining));
    }
    if (transient_happenned) {
        maa->scheduleIssueInstructionEvent();
    }
}
void Invalidator::finishInstruction(Instruction *instruction) {
    if (instruction->accessType == Instruction::AccessType::READ) {
        panic_if(rg_status[instruction->maa_id][instruction->addrRangeID] != RGStatus::UsingShared && rg_status[instruction->maa_id][instruction->addrRangeID] != RGStatus::UsingModified, "Instruction %s is not in UsingShared or UsingModified state: %s!\n", instruction->print(), rg_status_names[(uint8_t)(rg_status[instruction->maa_id][instruction->addrRangeID])]);
        if (rg_status[instruction->maa_id][instruction->addrRangeID] == RGStatus::UsingShared) {
            rg_status[instruction->maa_id][instruction->addrRangeID] = RGStatus::UsedShared;
            DPRINTF(MAAInvalidator, "Region[%d][%d] changed to UsedShared because of finishing READ for instruction %s!\n", instruction->maa_id, instruction->addrRangeID, instruction->print());
        } else if (rg_status[instruction->maa_id][instruction->addrRangeID] == RGStatus::UsingModified) {
            rg_status[instruction->maa_id][instruction->addrRangeID] = RGStatus::UsedModified;
            DPRINTF(MAAInvalidator, "Region[%d][%d] changed to UsedModified because of finishing READ for instruction %s!\n", instruction->maa_id, instruction->addrRangeID, instruction->print());
        }
    } else if (instruction->accessType == Instruction::AccessType::WRITE) {
        panic_if(rg_status[instruction->maa_id][instruction->addrRangeID] != RGStatus::UsingModified, "Instruction %s is not in UsingModified state: %s!\n", instruction->print(), rg_status_names[(uint8_t)(rg_status[instruction->maa_id][instruction->addrRangeID])]);
        rg_status[instruction->maa_id][instruction->addrRangeID] = RGStatus::UsedModified;
        DPRINTF(MAAInvalidator, "Region[%d][%d] changed to UsedModified because of finishing WRITE for instruction %s!\n", instruction->maa_id, instruction->addrRangeID, instruction->print());
    }
}
int Invalidator::get_cl_id(int tile_id, int element_id, int word_size) {
    return (int)((tile_id * num_tile_elements * 4 + element_id * word_size) / 64);
}
void Invalidator::read(int tile_id, int element_id) {
    assert((0 <= tile_id) && (tile_id < num_tiles));
    assert((0 <= element_id) && (element_id < num_tile_elements));
    int cl_id = get_cl_id(tile_id, element_id, 4);
    // It's possible that the data is cleanevict'ed or clear cleackwirteback'ed and MAA does not know
    // panic_if(cl_status[cl_id] != CLStatus::Uncached, "CL[%d] is not uncached, state: %s!\n",
    //          cl_id, cl_status[cl_id] == CLStatus::ReadCached ? "ReadCached" : "WriteCached");
    cl_status[cl_id] = CLStatus::ReadCached;
    DPRINTF(MAAInvalidator, "%s T[%d] E[%d] CL[%d]: read cached\n",
            __func__,
            tile_id,
            element_id,
            cl_id);
}
void Invalidator::write(int tile_id, int element_id) {
    assert((0 <= tile_id) && (tile_id < num_tiles));
    assert((0 <= element_id) && (element_id < num_tile_elements));
    int cl_id = get_cl_id(tile_id, element_id, 4);
    // It's possible that the data is cleanevict'ed or clear cleackwirteback'ed and MAA does not know
    // panic_if(cl_status[cl_id] != CLStatus::Uncached, "CL[%d] is not uncached, state: %s!\n",
    //          cl_id, cl_status[cl_id] == CLStatus::ReadCached ? "ReadCached" : "WriteCached");
    cl_status[cl_id] = CLStatus::WriteCached;
    DPRINTF(MAAInvalidator, "%s T[%d] E[%d] CL[%d]: write cached\n",
            __func__,
            tile_id,
            element_id,
            cl_id);
}
void Invalidator::setInstruction(Instruction *_instruction) {
    assert(my_instruction == nullptr);
    my_instruction = _instruction;
}
void Invalidator::executeInstruction() {
    switch (state) {
    case Status::Idle: {
        assert(my_instruction != nullptr);
        DPRINTF(MAAInvalidator, "%s: idling %s!\n", __func__, my_instruction->print());
        state = Status::Decode;
        [[fallthrough]];
    }
    case Status::Decode: {
        assert(my_instruction != nullptr);
        DPRINTF(MAAInvalidator, "%s: decoding %s!\n", __func__, my_instruction->print());

        // Decoding the instruction
        my_invalidating_tile = my_instruction->dst1Status == Instruction::TileStatus::Invalidating ? my_instruction->dst1SpdID : -1;
        my_invalidating_tile = (my_invalidating_tile == -1 && my_instruction->dst2Status == Instruction::TileStatus::Invalidating) ? my_instruction->dst2SpdID : my_invalidating_tile;
        my_invalidating_tile = (my_invalidating_tile == -1 && my_instruction->src1Status == Instruction::TileStatus::Invalidating) ? my_instruction->src1SpdID : my_invalidating_tile;
        my_invalidating_tile = (my_invalidating_tile == -1 && my_instruction->src2Status == Instruction::TileStatus::Invalidating) ? my_instruction->src2SpdID : my_invalidating_tile;
        my_invalidating_tile = (my_invalidating_tile == -1 && my_instruction->condStatus == Instruction::TileStatus::Invalidating) ? my_instruction->condSpdID : my_invalidating_tile;
        panic_if(my_invalidating_tile == -1, "No invalidating tile found!\n");
        my_word_size = my_instruction->getWordSize(my_invalidating_tile);

        // Initialization
        my_i = 0;
        my_last_block_addr = 0;
        my_outstanding_pkt = false;
        my_received_responses = 0;
        my_total_invalidations_sent = 0;
        my_decode_start_tick = curTick();
        maa->stats.numInst++;
        maa->stats.numInst_INV++;
        my_cl_id = -1;

        // Setting the state of the instruction and stream unit
        DPRINTF(MAAInvalidator, "%s: state set to request for request %s!\n", __func__, my_instruction->print());
        state = Status::Request;
        [[fallthrough]];
    }
    case Status::Request: {
        assert(my_instruction != nullptr);
        DPRINTF(MAAInvalidator, "%s: requesting %s!\n", __func__, my_instruction->print());
        if (my_outstanding_pkt) {
            if (sendOutstandingPacket() == false) {
                break;
            }
        }
        for (; my_i < num_tile_elements; my_i++) {
            my_cl_id = get_cl_id(my_invalidating_tile, my_i, my_word_size);
            if (cl_status[my_cl_id] == CLStatus::ReadCached || cl_status[my_cl_id] == CLStatus::WriteCached) {
                DPRINTF(MAAInvalidator, "%s T[%d] E[%d] CL[%d]: %s, invalidating\n",
                        __func__, my_invalidating_tile, my_i, my_cl_id, cl_status[my_cl_id] == CLStatus::ReadCached ? "ReadCached" : "WriteCached");
                Addr curr_block_addr = my_base_addr + my_cl_id * 64;
                if (curr_block_addr != my_last_block_addr) {
                    my_last_block_addr = curr_block_addr;
                    createMyPacket();
                    my_total_invalidations_sent++;
                    if (sendOutstandingPacket() == false) {
                        my_i++;
                        return;
                    }
                }
            }
        }
        DPRINTF(MAAInvalidator, "%s: state set to respond for request %s!\n", __func__, my_instruction->print());
        state = Status::Response;
        [[fallthrough]];
    }
    case Status::Response: {
        assert(my_instruction != nullptr);
        DPRINTF(MAAInvalidator, "%s: responding %s!\n", __func__, my_instruction->print());
        if (my_received_responses == my_total_invalidations_sent) {
            state = Status::Idle;
            maa->finishInstructionInvalidate(my_instruction, my_invalidating_tile);
            DPRINTF(MAAInvalidator, "%s: state set to idle for request %s!\n", __func__, my_instruction->print());
            my_instruction = nullptr;
            Cycles total_cycles = maa->getTicksToCycles(curTick() - my_decode_start_tick);
            maa->stats.cycles += total_cycles;
            maa->stats.cycles_INV += total_cycles;
            my_decode_start_tick = 0;
        }
        break;
    }
    default:
        assert(false);
    }
}
void Invalidator::createMyPacket() {
    /**** Packet generation ****/
    RequestPtr real_req = std::make_shared<Request>(my_last_block_addr, block_size, flags, maa->requestorId);
    my_pkt = new Packet(real_req, MemCmd::ReadExReq);
    my_outstanding_pkt = true;
    my_pkt->allocate();
    my_pkt->setExpressSnoop();
    my_pkt->headerDelay = my_pkt->payloadDelay = 0;
    DPRINTF(MAAInvalidator, "%s: created %s\n", __func__, my_pkt->print());
    (*maa->stats.INV_NumInvalidatedCachelines)++;
}
bool Invalidator::sendOutstandingPacket() {
    DPRINTF(MAAInvalidator, "%s: trying sending %s\n", __func__, my_pkt->print());
    if (maa->sendSnoopInvalidateCpu(my_pkt) == false) {
        DPRINTF(MAAInvalidator, "%s: send failed, leaving send packet...\n", __func__);
        return false;
    }
    if (my_pkt->cacheResponding() == true) {
        DPRINTF(MAAInvalidator, "INV %s: a cache in the O/M state will respond, send successfull...\n", __func__);
    } else if (my_pkt->hasSharers() == true) {
        my_received_responses++;
        cl_status[my_cl_id] = CLStatus::Uncached;
        DPRINTF(MAAInvalidator, "INV %s: There was a cache in the E/S state invalidated\n", __func__);
    } else {
        my_received_responses++;
        cl_status[my_cl_id] = CLStatus::Uncached;
        DPRINTF(MAAInvalidator, "INV %s: no cache responds (I)\n", __func__);
    }
    return true;
}
bool Invalidator::recvData(int tile_id, int element_id, uint8_t *dataptr) {
    assert((0 <= tile_id) && (tile_id < num_tiles));
    assert((0 <= element_id) && (element_id < num_tile_elements));
    int cl_id = get_cl_id(tile_id, element_id, 4);
    assert(cl_status[cl_id] != CLStatus::Uncached);
    cl_status[cl_id] = CLStatus::Uncached;
    DPRINTF(MAAInvalidator, "%s T[%d] E[%d-%d] CL[%d]: uncached\n", __func__, tile_id, element_id, element_id + 15, cl_id);
    my_received_responses++;
    uint32_t *dataptr_u32_typed = (uint32_t *)dataptr;
    for (int i = 0; i < 16; i++) {
        maa->spd->setData<uint32_t>(tile_id, element_id + i, dataptr_u32_typed[i]);
    }
    if (state == Status::Response && my_received_responses == my_total_invalidations_sent) {
        DPRINTF(MAAInvalidator, "%s: all words received, calling execution again!\n", __func__);
        scheduleExecuteInstructionEvent();
    } else {
        DPRINTF(MAAInvalidator, "%s: expected: %d, received: %d!\n", __func__, my_total_invalidations_sent, my_received_responses);
    }
    return true;
}
void Invalidator::scheduleExecuteInstructionEvent(int latency) {
    DPRINTF(MAAInvalidator, "%s: scheduling execute for the Invalidator Unit in the next %d cycles!\n", __func__, latency);
    Tick new_when = maa->getClockEdge(Cycles(latency));
    panic_if(executeInstructionEvent.scheduled(), "Event already scheduled!\n");
    maa->schedule(executeInstructionEvent, new_when);
}
void Invalidator::scheduleTransientInstructionEvent(int latency) {
    DPRINTF(MAAInvalidator, "%s: scheduling transient for the Invalidator Unit in the next %d cycles!\n", __func__, latency);
    panic_if(latency < 0, "Negative latency of %d!\n", latency);
    Tick new_when = maa->getClockEdge(Cycles(latency));
    if (!transientInstructionEvent.scheduled()) {
        maa->schedule(transientInstructionEvent, new_when);
    } else {
        Tick old_when = transientInstructionEvent.when();
        DPRINTF(MAAInvalidator, "%s: transition already scheduled for tick %d\n", __func__, old_when);
        if (new_when < old_when) {
            DPRINTF(MAAInvalidator, "%s: rescheduling for tick %d!\n", __func__, new_when);
            maa->reschedule(transientInstructionEvent, new_when);
        }
    }
}
} // namespace gem5