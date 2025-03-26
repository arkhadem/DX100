#include "mem/MAA/ALU.hh"
#include "mem/MAA/IF.hh"
#include "mem/MAA/IndirectAccess.hh"
#include "mem/MAA/Invalidator.hh"
#include "mem/MAA/RangeFuser.hh"
#include "mem/MAA/SPD.hh"
#include "mem/MAA/StreamAccess.hh"
#include "mem/MAA/MAA.hh"

#include "base/addr_range.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "mem/packet.hh"
#include "params/MAA.hh"
#include "debug/MAACpuPort.hh"
#include "debug/MAAController.hh"
#include <cassert>
#include <cstdint>

#ifndef TRACING_ON
#define TRACING_ON 1
#endif

namespace gem5 {

void MAA::recvTimingSnoopResp(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACpuPort, "%s: received %s\n", __func__, pkt->print());
    switch (pkt->cmd.toInt()) {
    case MemCmd::ReadExResp: {
        assert(pkt->getSize() == 64);
        for (int i = 0; i < 64; i += 4) {
            panic_if(pkt->req->getByteEnable()[i] == false, "Byte enable [%d] is not set for the read response\n", i);
        }
        AddressRangeType address_range = AddressRangeType(pkt->getAddr(), addrRanges);
        panic_if(address_range.isValid() == false, "Invalid address range: %s\n", address_range.print());
        assert(address_range.getType() == AddressRangeType::Type::SPD_DATA_CACHEABLE_RANGE);
        Addr offset = address_range.getOffset();
        int tile_id = offset / (num_tile_elements * sizeof(uint32_t));
        int element_id = offset % (num_tile_elements * sizeof(uint32_t));
        assert(element_id % sizeof(uint32_t) == 0);
        element_id /= sizeof(uint32_t);
        invalidator->recvData(tile_id, element_id, pkt->getPtr<uint8_t>());
        break;
    }
    default:
        assert(false);
    }
}
bool MAA::CpuSidePort::recvTimingSnoopResp(PacketPtr pkt) {
    assert(pkt->isResponse());
    /// print the packet
    DPRINTF(MAACpuPort, "%s: received %s\n", __func__, pkt->print());
    maa.recvTimingSnoopResp(pkt);
    outstandingCpuSidePackets--;
    if (is_blocked) {
        is_blocked = false;
        maa.invalidator->scheduleExecuteInstructionEvent();
    }
    pkt->deleteData();
    delete pkt;
    return true;
}

bool MAA::CpuSidePort::tryTiming(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACpuPort, "%s: received %s\n", __func__, pkt->print());
    return true;
}

void MAA::recvTimingReq(PacketPtr pkt, int core_id) {
    /// print the packet
    DPRINTF(MAACpuPort, "%s: received %s, cmd: %s, isMaskedWrite: %d, size: %d\n",
            __func__,
            pkt->print(),
            pkt->cmdString(),
            pkt->isMaskedWrite(),
            pkt->getSize());
    AddressRangeType address_range = AddressRangeType(pkt->getAddr(), addrRanges);
    DPRINTF(MAACpuPort, "%s: address range type: %s\n", __func__, address_range.print());
    for (int i = 0; i < pkt->getSize(); i++) {
        panic_if(pkt->req->getByteEnable()[i] == false, "Byte enable [%d] is not set for the request\n", i);
    }
    switch (pkt->cmd.toInt()) {
    case MemCmd::WritebackDirty: {
        assert(pkt->isMaskedWrite() == false);
        switch (address_range.getType()) {
        case AddressRangeType::Type::SPD_DATA_CACHEABLE_RANGE: {
            Addr offset = address_range.getOffset();
            int tile_id = offset / (num_tile_elements * sizeof(uint32_t));
            panic_if(pkt->getSize() != 64, "Invalid size for SPD data: %d\n", pkt->getSize());
            int element_id = (offset % (num_tile_elements * sizeof(uint32_t))) / sizeof(uint32_t);
            for (int i = 0; i < 64 / sizeof(uint32_t); i++) {
                uint32_t data = pkt->getPtr<uint32_t>()[i];
                DPRINTF(MAACpuPort, "%s: TILE[%d][%d] = %u\n", __func__, tile_id, element_id + i, data);
                spd->setData<uint32_t>(tile_id, element_id + i, data);
            }
            assert(pkt->needsResponse() == false);
            pendingDelete.reset(pkt);
            break;
        }
        default:
            panic_if(true, "%s: Error: Range(%s) and cmd(%s) is illegal. Packet: %s\n", __func__, address_range.print(), pkt->cmdString(), pkt->print());
            assert(false);
        }
        break;
    }
    case MemCmd::WriteReq: {
        bool respond_immediately = true;
        assert(pkt->isMaskedWrite() == false);
        switch (address_range.getType()) {
        case AddressRangeType::Type::SCALAR_RANGE: {
            panic_if(core_id != 0, "Scalar range is only for the core 0\n");
            Addr offset = address_range.getOffset();
            int element_id = offset % (num_regs * sizeof(uint32_t));
            assert(element_id % sizeof(uint32_t) == 0);
            element_id /= sizeof(uint32_t);
            panic_if(pkt->getSize() != 4 && pkt->getSize() != 8, "Invalid size for RF data: %d\n", pkt->getSize());
            RegisterPtr current_register = new Register();
            current_register->size = pkt->getSize();
            current_register->register_id = element_id;
            if (my_RID_to_core_id.find(pkt->requestorId()) == my_RID_to_core_id.end()) {
                int num_received_cores = my_RID_to_core_id.size();
                panic_if(num_received_cores == num_cores, "received more than %d instructions\n", num_cores);
                my_RID_to_core_id[pkt->requestorId()] = num_received_cores;
                num_received_cores++;
            }
            current_register->core_id = my_RID_to_core_id[pkt->requestorId()];
            current_register->maa_id = current_register->core_id % num_maas;
            if (pkt->getSize() == 4) {
                uint32_t data_UINT32 = pkt->getPtr<uint32_t>()[0];
                int32_t data_INT32 = pkt->getPtr<int32_t>()[0];
                float data_FLOAT = pkt->getPtr<float>()[0];
                DPRINTF(MAACpuPort, "%s: REG[%d] = %u/%d/%f\n", __func__, element_id, data_UINT32, data_INT32, data_FLOAT);
                current_register->data_UINT32 = data_UINT32;
                // rf->setData<uint32_t>(element_id, data_UINT32);
            } else {
                uint64_t data_UINT64 = pkt->getPtr<uint64_t>()[0];
                int64_t data_INT64 = pkt->getPtr<int64_t>()[0];
                double data_DOUBLE = pkt->getPtr<double>()[0];
                DPRINTF(MAACpuPort, "%s: REG[%d] = %lu/%ld/%lf\n", __func__, element_id, data_UINT64, data_INT64, data_DOUBLE);
                current_register->data_UINT64 = data_UINT64;
                // rf->setData<uint64_t>(element_id, data_UINT64);
            }
            my_registers.push_back(current_register);
            my_register_pkts.push_back(pkt);
            assert(pkt->needsResponse());
            respond_immediately = false;
            scheduleDispatchRegisterEvent();
            break;
        }
        case AddressRangeType::Type::INSTRUCTION_RANGE: {
            panic_if(core_id != 0, "Instruction range is only for the core 0\n");
            Addr offset = address_range.getOffset();
            int element_id = offset % (num_instructions_total * sizeof(uint64_t));
            assert(element_id % sizeof(uint64_t) == 0);
            element_id /= sizeof(uint64_t);
            uint64_t data = pkt->getPtr<uint64_t>()[0];
            DPRINTF(MAACpuPort, "%s: IF[%d] = %ld\n", __func__, element_id, data);
            InstructionPtr current_instruction;
            int instruction_id = -1;
            for (int i = 0; i < my_instruction_RIDs.size(); i++) {
                if (my_instruction_RIDs[i] == pkt->requestorId()) {
                    panic_if(instruction_id != -1, "Received multiple instructions from the same requestor\n");
                    panic_if(my_instruction_recvs[i], "Received new instruction after unissued instruction!\n");
                    current_instruction = my_instructions[i];
                    instruction_id = i;
                    my_instruction_pkts[i] = pkt;
                }
            }
            if (instruction_id == -1) {
                current_instruction = new Instruction();
                my_instruction_pkts.push_back(pkt);
                my_instruction_RIDs.push_back(pkt->requestorId());
                my_instruction_recvs.push_back(false);
                if (my_RID_to_core_id.find(pkt->requestorId()) == my_RID_to_core_id.end()) {
                    int num_received_cores = my_RID_to_core_id.size();
                    panic_if(num_received_cores == num_cores, "received more than %d instructions\n", num_cores);
                    my_RID_to_core_id[pkt->requestorId()] = num_received_cores;
                    num_received_cores++;
                }
                current_instruction->core_id = my_RID_to_core_id[pkt->requestorId()];
                current_instruction->maa_id = current_instruction->core_id % num_maas;
                my_instructions.push_back(current_instruction);
            }
#define NA_UINT8 0xFF
            switch (element_id) {
            case 0: {
                panic_if(instruction_id != -1, "Received new instruction[0] after incomplete instruction!\n");
                current_instruction->dst2SpdID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                current_instruction->dst1SpdID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                current_instruction->optype = (data & NA_UINT8) == NA_UINT8 ? Instruction::OPType::MAX : static_cast<Instruction::OPType>(data & NA_UINT8);
                data = data >> 8;
                current_instruction->datatype = (data & NA_UINT8) == NA_UINT8 ? Instruction::DataType::MAX : static_cast<Instruction::DataType>(data & NA_UINT8);
                assert(current_instruction->datatype != Instruction::DataType::MAX);
                data = data >> 8;
                current_instruction->opcode = (data & NA_UINT8) == NA_UINT8 ? Instruction::OpcodeType::MAX : static_cast<Instruction::OpcodeType>(data & NA_UINT8);
                assert(current_instruction->opcode != Instruction::OpcodeType::MAX);
                if (current_instruction->opcode == Instruction::OpcodeType::STREAM_LD ||
                    current_instruction->opcode == Instruction::OpcodeType::INDIR_LD) {
                    current_instruction->accessType = Instruction::AccessType::READ;
                } else if (current_instruction->opcode == Instruction::OpcodeType::STREAM_ST ||
                           current_instruction->opcode == Instruction::OpcodeType::INDIR_ST_SCALAR ||
                           current_instruction->opcode == Instruction::OpcodeType::INDIR_ST_VECTOR ||
                           current_instruction->opcode == Instruction::OpcodeType::INDIR_RMW_SCALAR ||
                           current_instruction->opcode == Instruction::OpcodeType::INDIR_RMW_VECTOR) {
                    current_instruction->accessType = Instruction::AccessType::WRITE;
                } else {
                    current_instruction->accessType = Instruction::AccessType::COMPUTE;
                }
                break;
            }
            case 1: {
                panic_if(instruction_id == -1, "Received new instruction[1] before insturction[0]!\n");
                current_instruction->condSpdID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                current_instruction->src3RegID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                current_instruction->src2RegID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                current_instruction->src1RegID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                current_instruction->dst2RegID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                current_instruction->dst1RegID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                current_instruction->src2SpdID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                current_instruction->src1SpdID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                break;
            }
            case 2: {
                panic_if(instruction_id == -1, "Received new instruction[2] before insturction[0]!\n");
                current_instruction->baseAddr = data;
                current_instruction->state = Instruction::Status::Idle;
                current_instruction->CID = pkt->req->contextId();
                current_instruction->PC = pkt->req->getPC();
                if (current_instruction->accessType != Instruction::AccessType::COMPUTE) {
                    current_instruction->addrRangeID = getAddrRegion(current_instruction->baseAddr);
                    current_instruction->minAddr = addrRegions[current_instruction->addrRangeID].first;
                    current_instruction->maxAddr = addrRegions[current_instruction->addrRangeID].second;
                }
                my_instruction_recvs[instruction_id] = true;
                DPRINTF(MAAController, "%s: %s received!\n", __func__, current_instruction->print());
                respond_immediately = false;
                scheduleDispatchInstructionEvent();
                break;
            }
            default:
                assert(false);
            }
            assert(pkt->needsResponse());
            if (respond_immediately) {
                pkt->makeTimingResponse();
                // Here we reset the timing of the packet.
                Tick old_header_delay = pkt->headerDelay;
                pkt->headerDelay = pkt->payloadDelay = 0;
                cpuSidePorts[core_id]->schedTimingResp(pkt, getClockEdge(Cycles(1)) + old_header_delay);
            }
            break;
        }
        default:
            // Write to SPD_DATA_CACHEABLE_RANGE not possible. All SPD writes must be to SPD_DATA_NONCACHEABLE_RANGE
            // Write to SPD_SIZE_RANGE not possible. Size is read-only.
            // Write to SPD_READY_RANGE not possible. Ready is read-only.
            panic_if(true, "%s: Error: Range(%s) and cmd(%s) is illegal. Packet: %s\n", __func__, address_range.print(), pkt->cmdString(), pkt->print());
            assert(false);
        }
        break;
    }
    case MemCmd::ReadReq: {
        // all read responses have a data payload
        assert(pkt->hasRespData());
        switch (address_range.getType()) {
        case AddressRangeType::Type::SPD_SIZE_RANGE: {
            panic_if(core_id != 0, "Size range is only for the core 0\n");
            panic_if(pkt->getSize() != sizeof(uint16_t), "%s: Error: Invalid size for SPD size: %d, packet: %s\n", __func__, pkt->getSize(), pkt->print());
            Addr offset = address_range.getOffset();
            assert(offset % sizeof(uint16_t) == 0);
            int element_id = offset / sizeof(uint16_t);
            uint16_t data = spd->getSize(element_id);
            uint8_t *dataPtr = (uint8_t *)(&data);
            pkt->setData(dataPtr);
            assert(pkt->needsResponse());
            pkt->makeTimingResponse();
            // Here we reset the timing of the packet.
            Tick old_header_delay = pkt->headerDelay;
            pkt->headerDelay = pkt->payloadDelay = 0;
            cpuSidePorts[core_id]->schedTimingResp(pkt, getClockEdge(Cycles(1)) + old_header_delay);
            break;
        }
        case AddressRangeType::Type::SPD_READY_RANGE: {
            panic_if(core_id != 0, "Ready range is only for the core 0\n");
            panic_if(pkt->getSize() != sizeof(uint16_t), "%s: Error: Invalid size for SPD ready: %d, packet: %s\n", __func__, pkt->getSize(), pkt->print());
            Addr offset = address_range.getOffset();
            assert(offset % sizeof(uint16_t) == 0);
            int ready_tile_id = offset / sizeof(uint16_t);
            const uint16_t one = 1;
            pkt->setData((const uint8_t *)&one);
            assert(pkt->needsResponse());
            if (spd->getTileReady(ready_tile_id)) {
                pkt->makeTimingResponse();
                // Here we reset the timing of the packet.
                Tick old_header_delay = pkt->headerDelay;
                pkt->headerDelay = pkt->payloadDelay = 0;
                cpuSidePorts[core_id]->schedTimingResp(pkt, getClockEdge(Cycles(1)) + old_header_delay);
            } else {
                // We need to respond to this packet later
                my_ready_pkts.push_back(pkt);
                my_ready_tile_ids.push_back(ready_tile_id);
            }
            break;
        }
        case AddressRangeType::Type::SCALAR_RANGE: {
            panic_if(core_id != 0, "Scalar range is only for the core 0\n");
            panic_if(pkt->getSize() != 4 && pkt->getSize() != 8, "Invalid size for SPD data: %d\n", pkt->getSize());
            Addr offset = address_range.getOffset();
            int element_id = offset % (num_regs * sizeof(uint32_t));
            assert(element_id % sizeof(uint32_t) == 0);
            element_id /= sizeof(uint32_t);
            uint8_t *dataPtr = rf->getDataPtr(element_id);
            pkt->setData(dataPtr);
            assert(pkt->needsResponse());
            pkt->makeTimingResponse();
            // Here we reset the timing of the packet.
            Tick old_header_delay = pkt->headerDelay;
            pkt->headerDelay = pkt->payloadDelay = 0;
            cpuSidePorts[core_id]->schedTimingResp(pkt, getClockEdge(Cycles(1)) + old_header_delay);
            break;
        }
        default: {
            // Read from SPD_DATA_CACHEABLE_RANGE uses ReadSharedReq command.
            // Read from SPD_DATA_NONCACHEABLE_RANGE not possible. All SPD reads must be from SPD_DATA_CACHEABLE_RANGE.
            panic_if(true, "%s: Error: Range(%s) and cmd(%s) is illegal. Packet: %s\n", __func__, address_range.print(), pkt->cmdString(), pkt->print());
            assert(false);
        }
        }
        break;
    }
    case MemCmd::ReadExReq:
    case MemCmd::ReadSharedReq: {
        // all read responses have a data payload
        assert(pkt->hasRespData());
        switch (address_range.getType()) {
        case AddressRangeType::Type::SPD_DATA_CACHEABLE_RANGE: {
            Addr offset = address_range.getOffset();
            int tile_id = offset / (num_tile_elements * sizeof(uint32_t));
            int element_id = offset % (num_tile_elements * sizeof(uint32_t));
            assert(element_id % sizeof(uint32_t) == 0);
            element_id /= sizeof(uint32_t);
            spd->setTileDirty(tile_id, 4);
            if (pkt->cmd == MemCmd::ReadSharedReq) {
                invalidator->read(tile_id, element_id);
            } else {
                invalidator->write(tile_id, element_id);
            }
            uint8_t *dataPtr = spd->getDataPtr(tile_id, element_id);
            pkt->setData(dataPtr);
            assert(pkt->needsResponse());
            pkt->makeTimingResponse();
            // Here we reset the timing of the packet.
            Tick old_header_delay = pkt->headerDelay;
            pkt->headerDelay = pkt->payloadDelay = 0;
            cpuSidePorts[core_id]->schedTimingResp(pkt, getClockEdge(Cycles(1)) + old_header_delay);
            break;
        }
        default:
            panic_if(true, "%s: Error: Range(%s) and cmd(%s) is illegal. Packet: %s\n", __func__, address_range.print(), pkt->cmdString(), pkt->print());
            assert(false);
        }
        break;
    }
    default:
        assert(false);
    }
}
bool MAA::CpuSidePort::recvTimingReq(PacketPtr pkt) {
    assert(pkt->isRequest());
    /// print the packet
    DPRINTF(MAACpuPort, "%s: received %s\n", __func__, pkt->print());

    if (tryTiming(pkt)) {
        maa.recvTimingReq(pkt, core_id);
        return true;
    }
    return false;
}
void MAA::CpuSidePort::recvFunctional(PacketPtr pkt) {
    assert(false);
}
Tick MAA::recvAtomic(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACpuPort, "%s: received %s\n", __func__, pkt->print());
    assert(false);
    return 0;
}
Tick MAA::CpuSidePort::recvAtomic(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACpuPort, "%s: received %s\n", __func__, pkt->print());
    assert(false);
    return maa.recvAtomic(pkt);
}

AddrRangeList MAA::CpuSidePort::getAddrRanges() const {
    return maa.getAddrRanges(core_id);
}

bool MAA::CpuSidePort::sendSnoopInvalidatePacket(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACpuPort, "%s: sending invalidation %s\n", __func__, pkt->print());
    int pkt_core_id = maa.core_addr(pkt->getAddr());
    panic_if(pkt_core_id != core_id, "%s: packet is for core %d\n", __func__, pkt_core_id);
    panic_if(is_blocked, "%s: port is blocked\n", __func__);
    if (outstandingCpuSidePackets == maxOutstandingCpuSidePackets) {
        // XBAR is full
        DPRINTF(MAACpuPort, "%s Send failed because XBAR is full...\n", __func__);
        is_blocked = true;
        return false;
    }
    sendTimingSnoopReq(pkt);
    DPRINTF(MAACpuPort, "%s Send is successfull...\n", __func__);
    if (pkt->cacheResponding())
        outstandingCpuSidePackets++;
    return true;
}
bool MAA::sendSnoopInvalidateCpu(PacketPtr pkt) {
    panic_if(pkt->isExpressSnoop() == false, "Packet is not an express snoop packet\n");
    int pkt_core_id = core_addr(pkt->getAddr());
    return cpuSidePorts[pkt_core_id]->sendSnoopInvalidatePacket(pkt);
}

void MAA::sendSnoopPacketCpu(PacketPtr pkt) {
    panic_if(pkt->isExpressSnoop() == false, "Packet is not an express snoop packet\n");
    int pkt_core_id = core_addr(pkt->getAddr());
    cpuSidePorts[pkt_core_id]->sendTimingSnoopReq(pkt);
}

void MAA::CpuSidePort::allocate(int _core_id, int _maxOutstandingCpuSidePackets) {
    outstandingCpuSidePackets = 0;
    core_id = _core_id;
    maxOutstandingCpuSidePackets = _maxOutstandingCpuSidePackets - 16;
    is_blocked = false;
}

MAA::CpuSidePort::CpuSidePort(const std::string &_name, MAA &_maa,
                              const std::string &_label)
    : MAAResponsePort(_name, _maa, _label) {
}
} // namespace gem5