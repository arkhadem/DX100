#include "mem/MAA/IF.hh"
#include "mem/MAA/MAA.hh"
#include "debug/MAAController.hh"
#include "mem/MAA/SPD.hh"
#include <cassert>

#ifndef TRACING_ON
#define TRACING_ON 1
#endif

namespace gem5 {
Instruction::Instruction() : baseAddr(0xFFFFFFFFFFFFFFFF),
                             src1RegID(-1),
                             src2RegID(-1),
                             src3RegID(-1),
                             dst1RegID(-1),
                             dst2RegID(-1),
                             src1SpdID(-1),
                             src2SpdID(-1),
                             src1Status(TileStatus::WaitForInvalidation),
                             src2Status(TileStatus::WaitForInvalidation),
                             dst1SpdID(-1),
                             dst2SpdID(-1),
                             dst1Status(TileStatus::WaitForInvalidation),
                             dst2Status(TileStatus::WaitForInvalidation),
                             condSpdID(-1),
                             condStatus(TileStatus::WaitForInvalidation),
                             opcode(OpcodeType::MAX),
                             optype(OPType::MAX),
                             datatype(DataType::MAX),
                             state(Status::Idle),
                             funcUniType(FuncUnitType::MAX),
                             CID(-1),
                             PC(0),
                             if_id(-1),
                             core_id(-1),
                             maa_id(-1) {}
std::string Instruction::print() const {
    char baseAddrStr[32];
    std::sprintf(baseAddrStr, "0x%lx", baseAddr);
    char minAddrStr[32];
    std::sprintf(minAddrStr, "0x%lx", minAddr);
    char maxAddrStr[32];
    std::sprintf(maxAddrStr, "0x%lx", maxAddr);
    std::ostringstream str;
    ccprintf(str, "INSTR[%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s]",
             "core_id(" + std::to_string(core_id) + ")",
             " maa_id(" + std::to_string(maa_id) + ")",
             " opcode(" + opcode_names[(int)opcode] + ")",
             optype == OPType::MAX ? "" : " optype(" + optype_names[(int)optype] + ")",
             " datatype(" + datatype_names[(int)datatype] + ")",
             " state(" + status_names[(int)state] + ")",
             src1SpdID == -1 ? "" : " srcSPD1(" + std::to_string(src1SpdID) + "/" + tile_status_names[(uint8_t)src1Status] + ")",
             src2SpdID == -1 ? "" : " srcSPD2(" + std::to_string(src2SpdID) + "/" + tile_status_names[(uint8_t)src2Status] + ")",
             src1RegID == -1 ? "" : " srcREG1(" + std::to_string(src1RegID) + ")",
             src2RegID == -1 ? "" : " srcREG2(" + std::to_string(src2RegID) + ")",
             src3RegID == -1 ? "" : " srcREG3(" + std::to_string(src3RegID) + ")",
             dst1SpdID == -1 ? "" : " dstSPD1(" + std::to_string(dst1SpdID) + "/" + tile_status_names[(uint8_t)dst1Status] + ")",
             dst2SpdID == -1 ? "" : " dstSPD2(" + std::to_string(dst2SpdID) + "/" + tile_status_names[(uint8_t)dst2Status] + ")",
             dst1RegID == -1 ? "" : " dstREG1(" + std::to_string(dst1RegID) + ")",
             dst2RegID == -1 ? "" : " dstREG2(" + std::to_string(dst2RegID) + ")",
             condSpdID == -1 ? "" : " condSPD(" + std::to_string(condSpdID) + "/" + tile_status_names[(uint8_t)condStatus] + ")",
             baseAddr != 0xFFFFFFFFFFFFFFFF ? " baseAddr(" + std::string(baseAddrStr) + ") minAddr(" + std::string(minAddrStr) + ") maxAddr(" + std::string(maxAddrStr) + ")" : "");
    return str.str();
}
int Instruction::getWordSize(int tile_id) {
    panic_if(tile_id == -1, "Invalid tile_id %d!\n", tile_id);
    if (tile_id == condSpdID) {
        return 4;
    } else if (tile_id == src1SpdID) {
        switch (opcode) {
        case OpcodeType::ALU_SCALAR:
        case OpcodeType::ALU_VECTOR:
        case OpcodeType::ALU_REDUCE:
        case OpcodeType::STREAM_ST: {
            return WordSize();
        }
        case OpcodeType::INDIR_LD:
        case OpcodeType::INDIR_ST_VECTOR:
        case OpcodeType::INDIR_ST_SCALAR:
        case OpcodeType::INDIR_RMW_VECTOR:
        case OpcodeType::INDIR_RMW_SCALAR:
        case OpcodeType::RANGE_LOOP: {
            return 4;
        }
        default:
            assert(false);
        }
    } else if (tile_id == src2SpdID) {
        switch (opcode) {
        case OpcodeType::INDIR_ST_VECTOR:
        case OpcodeType::INDIR_RMW_VECTOR:
        case OpcodeType::ALU_VECTOR: {
            return WordSize();
        }
        case OpcodeType::RANGE_LOOP: {
            return 4;
        }
        default:
            assert(false);
        }
    } else if (tile_id == dst1SpdID) {
        switch (opcode) {
        case OpcodeType::ALU_SCALAR:
        case OpcodeType::ALU_VECTOR: {
            if (optype == OPType::GT_OP || optype == OPType::GTE_OP || optype == OPType::LT_OP || optype == OPType::LTE_OP || optype == OPType::EQ_OP) {
                return 4;
            } else {
                return WordSize();
            }
        }
        case OpcodeType::STREAM_LD:
        case OpcodeType::INDIR_LD:
        case OpcodeType::INDIR_ST_VECTOR:
        case OpcodeType::INDIR_ST_SCALAR:
        case OpcodeType::INDIR_RMW_VECTOR:
        case OpcodeType::INDIR_RMW_SCALAR: {
            return WordSize();
        }
        case OpcodeType::RANGE_LOOP: {
            return 4;
        }
        default:
            assert(false);
        }
    } else if (tile_id == dst2SpdID) {
        switch (opcode) {
        case OpcodeType::RANGE_LOOP: {
            return 4;
        }
        default:
            assert(false);
        }
    } else {
        assert(false);
    }
    assert(false);
    return -1;
}
int Instruction::WordSize() {
    switch (datatype) {
    case DataType::UINT32_TYPE:
    case DataType::INT32_TYPE:
    case DataType::FLOAT32_TYPE:
        return 4;
    case DataType::UINT64_TYPE:
    case DataType::INT64_TYPE:
    case DataType::FLOAT64_TYPE:
        return 8;
    default:
        assert(false);
    }
    assert(false);
    return -1;
}
bool IF::pushInstruction(Instruction _instruction) {
    switch (_instruction.opcode) {
    case Instruction::OpcodeType::STREAM_LD:
    case Instruction::OpcodeType::STREAM_ST: {
        _instruction.funcUniType = FuncUnitType::STREAM;
        break;
    }
    case Instruction::OpcodeType::INDIR_LD:
    case Instruction::OpcodeType::INDIR_ST_VECTOR:
    case Instruction::OpcodeType::INDIR_ST_SCALAR:
    case Instruction::OpcodeType::INDIR_RMW_VECTOR:
    case Instruction::OpcodeType::INDIR_RMW_SCALAR: {
        _instruction.funcUniType = FuncUnitType::INDIRECT;
        break;
    }
    case Instruction::OpcodeType::RANGE_LOOP: {
        _instruction.funcUniType = FuncUnitType::RANGE;
        break;
    }
    case Instruction::OpcodeType::ALU_SCALAR:
    case Instruction::OpcodeType::ALU_VECTOR:
    case Instruction::OpcodeType::ALU_REDUCE: {
        _instruction.funcUniType = FuncUnitType::ALU;
        break;
    }
    default: {
        assert(false);
    }
    }
    int free_instruction_slot = -1;
    int maa_id = _instruction.maa_id;
    for (int i = 0; i < num_instructions_per_maa; i++) {
        if (valids[maa_id][i] == false) {
            if (free_instruction_slot == -1) {
                free_instruction_slot = i;
            }
        } else {
            if (_instruction.dst1SpdID != -1) {
                if ((instructions[maa_id][i].dst1SpdID != -1 && _instruction.dst1SpdID == instructions[maa_id][i].dst1SpdID) ||
                    (instructions[maa_id][i].dst2SpdID != -1 && _instruction.dst1SpdID == instructions[maa_id][i].dst2SpdID) ||
                    (instructions[maa_id][i].src1SpdID != -1 && _instruction.dst1SpdID == instructions[maa_id][i].src1SpdID) ||
                    (instructions[maa_id][i].src2SpdID != -1 && _instruction.dst1SpdID == instructions[maa_id][i].src2SpdID) ||
                    (instructions[maa_id][i].condSpdID != -1 && _instruction.dst1SpdID == instructions[maa_id][i].condSpdID)) {
                    DPRINTF(MAAController, "%s: %s cannot be pushed b/c of %s!\n", __func__, _instruction.print(), instructions[maa_id][i].print());
                    return false;
                }
            }
            if (_instruction.dst2SpdID != -1) {
                if ((instructions[maa_id][i].dst1SpdID != -1 && _instruction.dst2SpdID == instructions[maa_id][i].dst1SpdID) ||
                    (instructions[maa_id][i].dst2SpdID != -1 && _instruction.dst2SpdID == instructions[maa_id][i].dst2SpdID) ||
                    (instructions[maa_id][i].src1SpdID != -1 && _instruction.dst2SpdID == instructions[maa_id][i].src1SpdID) ||
                    (instructions[maa_id][i].src2SpdID != -1 && _instruction.dst2SpdID == instructions[maa_id][i].src2SpdID) ||
                    (instructions[maa_id][i].condSpdID != -1 && _instruction.dst2SpdID == instructions[maa_id][i].condSpdID)) {
                    DPRINTF(MAAController, "%s: %s cannot be pushed b/c of %s!\n", __func__, _instruction.print(), instructions[maa_id][i].print());
                    return false;
                }
            }
            if ((_instruction.accessType == Instruction::AccessType::WRITE) &&
                (instructions[maa_id][i].accessType != Instruction::AccessType::COMPUTE) &&
                (_instruction.addrRangeID == instructions[maa_id][i].addrRangeID)) {
                DPRINTF(MAAController, "%s: %s cannot be pushed b/c of %s!\n", __func__, _instruction.print(), instructions[maa_id][i].print());
                return false;
            }
        }
    }
    if (free_instruction_slot == -1) {
        DPRINTF(MAAController, "%s: %s cannot be pushed b/c of no space!\n", __func__, _instruction.print());
        return false;
    }
    assert(free_instruction_slot < num_instructions_per_maa);
    instructions[maa_id][free_instruction_slot] = _instruction;
    valids[maa_id][free_instruction_slot] = true;
    instructions[maa_id][free_instruction_slot].if_id = free_instruction_slot;
    DPRINTF(MAAController, "%s: %s pushed to instruction[%d]!\n", __func__, _instruction.print(), free_instruction_slot);
    return true;
}
bool IF::canPushRegister(Register _reg) {
    int register_id = _reg.register_id;
    int maa_id = _reg.maa_id;
    for (int i = 0; i < num_instructions_per_maa; i++) {
        if (valids[maa_id][i] == true) {
            if ((instructions[maa_id][i].dst1RegID == register_id) ||
                (instructions[maa_id][i].dst2RegID == register_id) ||
                (instructions[maa_id][i].src1RegID == register_id) ||
                (instructions[maa_id][i].src2RegID == register_id) ||
                (instructions[maa_id][i].src3RegID == register_id)) {
                DPRINTF(MAAController, "%s: register write %d cannot be pushed b/c of %s!\n", __func__, register_id, instructions[maa_id][i].print());
                return false;
            }
        }
    }
    return true;
}
Instruction *IF::getReady(FuncUnitType funcUniType, int maa_id) {
    int rand_base = rand() % num_instructions_per_maa;
    if (funcUniType == FuncUnitType::INVALIDATOR) {
        panic_if(maa_id != -1, "Invalidator cannot have maa_id!\n");
        int mid_base = rand() % num_maas;
        for (int mid_offset = 0; mid_offset < num_maas; mid_offset++) {
            int mid = (mid_base + mid_offset) % num_maas;
            for (int i = 0; i < num_instructions_per_maa; i++) {
                int instr_idx = (rand_base + i) % num_instructions_per_maa;
                if (valids[mid][instr_idx] && instructions[mid][instr_idx].state == Instruction::Status::Idle) {
                    int tile_id = -1;
                    if (instructions[mid][instr_idx].dst1Status == Instruction::TileStatus::WaitForInvalidation) {
                        tile_id = instructions[mid][instr_idx].dst1SpdID;
                    } else if (instructions[mid][instr_idx].dst2Status == Instruction::TileStatus::WaitForInvalidation) {
                        tile_id = instructions[mid][instr_idx].dst2SpdID;
                    } else if (instructions[mid][instr_idx].src1Status == Instruction::TileStatus::WaitForInvalidation) {
                        tile_id = instructions[mid][instr_idx].src1SpdID;
                    } else if (instructions[mid][instr_idx].src2Status == Instruction::TileStatus::WaitForInvalidation) {
                        tile_id = instructions[mid][instr_idx].src2SpdID;
                    } else if (instructions[mid][instr_idx].condStatus == Instruction::TileStatus::WaitForInvalidation) {
                        tile_id = instructions[mid][instr_idx].condSpdID;
                    }
                    if (tile_id != -1) {
                        issueInstructionInvalidate(&instructions[mid][instr_idx], tile_id);
                        DPRINTF(MAAController, "%s: returned instruction[%d] %s for invalidation!\n", __func__, instr_idx, instructions[mid][instr_idx].print());
                        return &instructions[mid][instr_idx];
                    }
                }
            }
        }
    } else {
        panic_if(maa_id == -1, "Invalidator cannot have maa_id!\n");
        for (int i = 0; i < num_instructions_per_maa; i++) {
            int instr_idx = (rand_base + i) % num_instructions_per_maa;
            if (valids[maa_id][instr_idx] &&
                instructions[maa_id][instr_idx].maa_id == maa_id &&
                instructions[maa_id][instr_idx].state == Instruction::Status::Idle &&
                (instructions[maa_id][instr_idx].src1SpdID == -1 || instructions[maa_id][instr_idx].src1Status == Instruction::TileStatus::Service || instructions[maa_id][instr_idx].src1Status == Instruction::TileStatus::Finished) &&
                (instructions[maa_id][instr_idx].src2SpdID == -1 || instructions[maa_id][instr_idx].src2Status == Instruction::TileStatus::Service || instructions[maa_id][instr_idx].src2Status == Instruction::TileStatus::Finished) &&
                (instructions[maa_id][instr_idx].condSpdID == -1 || instructions[maa_id][instr_idx].condStatus == Instruction::TileStatus::Service || instructions[maa_id][instr_idx].condStatus == Instruction::TileStatus::Finished) &&
                (instructions[maa_id][instr_idx].dst1SpdID == -1 || instructions[maa_id][instr_idx].dst1Status == Instruction::TileStatus::WaitForService) &&
                (instructions[maa_id][instr_idx].dst2SpdID == -1 || instructions[maa_id][instr_idx].dst2Status == Instruction::TileStatus::WaitForService) &&
                instructions[maa_id][instr_idx].funcUniType == funcUniType) {
                if (maa->num_maas == 1 || maa->getAddrRegionPermit(&instructions[maa_id][instr_idx])) {
                    issueInstructionCompute(&instructions[maa_id][instr_idx]);
                    DPRINTF(MAAController, "%s: returned instruction[%d] %s for execute!\n", __func__, instr_idx, instructions[maa_id][instr_idx].print());
                    return &instructions[maa_id][instr_idx];
                }
            }
        }
    }
    return nullptr;
}
void IF::finishInstructionCompute(Instruction *instruction) {
    instruction->state = Instruction::Status::Finish;
    int maa_id = instruction->maa_id;
    valids[maa_id][instruction->if_id] = false;
    if (instruction->dst1SpdID != -1) {
        for (int i = 0; i < num_instructions_per_maa; i++) {
            if (valids[maa_id][i]) {
                if (instructions[maa_id][i].src1SpdID == instruction->dst1SpdID) {
                    instructions[maa_id][i].src1Status = Instruction::TileStatus::Finished;
                }
                if (instructions[maa_id][i].src2SpdID == instruction->dst1SpdID) {
                    instructions[maa_id][i].src2Status = Instruction::TileStatus::Finished;
                }
                if (instructions[maa_id][i].condSpdID == instruction->dst1SpdID) {
                    instructions[maa_id][i].condStatus = Instruction::TileStatus::Finished;
                }
            }
        }
    }
    if (instruction->dst2SpdID != -1) {
        for (int i = 0; i < num_instructions_per_maa; i++) {
            if (valids[maa_id][i]) {
                if (instructions[maa_id][i].src1SpdID == instruction->dst2SpdID) {
                    instructions[maa_id][i].src1Status = Instruction::TileStatus::Finished;
                }
                if (instructions[maa_id][i].src2SpdID == instruction->dst2SpdID) {
                    instructions[maa_id][i].src2Status = Instruction::TileStatus::Finished;
                }
                if (instructions[maa_id][i].condSpdID == instruction->dst2SpdID) {
                    instructions[maa_id][i].condStatus = Instruction::TileStatus::Finished;
                }
            }
        }
    }
}
Instruction::TileStatus IF::getTileStatus(int tile_id, uint8_t tile_status) {
    if (tile_status == (uint8_t)SPD::TileStatus::Idle) {
        return Instruction::TileStatus::WaitForService;
    } else if (tile_status == (uint8_t)SPD::TileStatus::Service) {
        return Instruction::TileStatus::Service;
    } else if (tile_status == (uint8_t)SPD::TileStatus::Finished) {
        return Instruction::TileStatus::Finished;
    } else {
        assert(false);
    }
    assert(false);
    return Instruction::TileStatus::WaitForService;
}
void IF::finishInstructionInvalidate(Instruction *instruction, int tile_id, uint8_t tile_status) {
    instruction->state = Instruction::Status::Idle;
    Instruction::TileStatus new_tile_status = getTileStatus(tile_id, tile_status);
    int maa_id = instruction->maa_id;
    for (int i = 0; i < num_instructions_per_maa; i++) {
        if (valids[maa_id][i]) {
            if (instructions[maa_id][i].src1SpdID == tile_id && instructions[maa_id][i].src1Status == Instruction::TileStatus::Invalidating) {
                instructions[maa_id][i].src1Status = new_tile_status;
            }
            if (instructions[maa_id][i].src2SpdID == tile_id && instructions[maa_id][i].src2Status == Instruction::TileStatus::Invalidating) {
                instructions[maa_id][i].src2Status = new_tile_status;
            }
            if (instructions[maa_id][i].condSpdID == tile_id && instructions[maa_id][i].condStatus == Instruction::TileStatus::Invalidating) {
                instructions[maa_id][i].condStatus = new_tile_status;
            }
            if (instructions[maa_id][i].dst1SpdID == tile_id && instructions[maa_id][i].dst1Status == Instruction::TileStatus::Invalidating) {
                instructions[maa_id][i].dst1Status = new_tile_status;
            }
            if (instructions[maa_id][i].dst2SpdID == tile_id && instructions[maa_id][i].dst2Status == Instruction::TileStatus::Invalidating) {
                instructions[maa_id][i].dst2Status = new_tile_status;
            }
        }
    }
}
void IF::issueInstructionCompute(Instruction *instruction) {
    instruction->state = Instruction::Status::Service;
    int maa_id = instruction->maa_id;
    if (instruction->dst1SpdID != -1) {
        for (int i = 0; i < num_instructions_per_maa; i++) {
            if (valids[maa_id][i]) {
                if (instructions[maa_id][i].src1SpdID == instruction->dst1SpdID) {
                    instructions[maa_id][i].src1Status = Instruction::TileStatus::Service;
                }
                if (instructions[maa_id][i].src2SpdID == instruction->dst1SpdID) {
                    instructions[maa_id][i].src2Status = Instruction::TileStatus::Service;
                }
                if (instructions[maa_id][i].condSpdID == instruction->dst1SpdID) {
                    instructions[maa_id][i].condStatus = Instruction::TileStatus::Service;
                }
                if (instructions[maa_id][i].dst1SpdID == instruction->dst1SpdID) {
                    instructions[maa_id][i].dst1Status = Instruction::TileStatus::Service;
                }
                if (instructions[maa_id][i].dst2SpdID == instruction->dst1SpdID) {
                    instructions[maa_id][i].dst2Status = Instruction::TileStatus::Service;
                }
            }
        }
    }
    if (instruction->dst2SpdID != -1) {
        for (int i = 0; i < num_instructions_per_maa; i++) {
            if (valids[maa_id][i]) {
                if (instructions[maa_id][i].src1SpdID == instruction->dst2SpdID) {
                    instructions[maa_id][i].src1Status = Instruction::TileStatus::Service;
                }
                if (instructions[maa_id][i].src2SpdID == instruction->dst2SpdID) {
                    instructions[maa_id][i].src2Status = Instruction::TileStatus::Service;
                }
                if (instructions[maa_id][i].condSpdID == instruction->dst2SpdID) {
                    instructions[maa_id][i].condStatus = Instruction::TileStatus::Service;
                }
                if (instructions[maa_id][i].dst1SpdID == instruction->dst2SpdID) {
                    instructions[maa_id][i].dst1Status = Instruction::TileStatus::Service;
                }
                if (instructions[maa_id][i].dst2SpdID == instruction->dst2SpdID) {
                    instructions[maa_id][i].dst2Status = Instruction::TileStatus::Service;
                }
            }
        }
    }
}
void IF::issueInstructionInvalidate(Instruction *instruction, int tile_id) {
    instruction->state = Instruction::Status::Service;
    int maa_id = instruction->maa_id;
    for (int i = 0; i < num_instructions_per_maa; i++) {
        if (valids[maa_id][i]) {
            if (instructions[maa_id][i].src1SpdID == tile_id && instructions[maa_id][i].src1Status == Instruction::TileStatus::WaitForInvalidation) {
                instructions[maa_id][i].src1Status = Instruction::TileStatus::Invalidating;
            }
            if (instructions[maa_id][i].src2SpdID == tile_id && instructions[maa_id][i].src2Status == Instruction::TileStatus::WaitForInvalidation) {
                instructions[maa_id][i].src2Status = Instruction::TileStatus::Invalidating;
            }
            if (instructions[maa_id][i].condSpdID == tile_id && instructions[maa_id][i].condStatus == Instruction::TileStatus::WaitForInvalidation) {
                instructions[maa_id][i].condStatus = Instruction::TileStatus::Invalidating;
            }
            if (instructions[maa_id][i].dst1SpdID == tile_id && instructions[maa_id][i].dst1Status == Instruction::TileStatus::WaitForInvalidation) {
                instructions[maa_id][i].dst1Status = Instruction::TileStatus::Invalidating;
            }
            if (instructions[maa_id][i].dst2SpdID == tile_id && instructions[maa_id][i].dst2Status == Instruction::TileStatus::WaitForInvalidation) {
                instructions[maa_id][i].dst2Status = Instruction::TileStatus::Invalidating;
            }
        }
    }
}
AddressRangeType::AddressRangeType(Addr _addr, AddrRangeList addrRanges) : addr(_addr) {
    valid = false;
    rangeID = 0;
    for (const auto &r : addrRanges) {
        if (r.contains(addr)) {
            base = r.start();
            offset = addr - base;
            valid = true;
            break;
        }
        rangeID++;
    }
}
std::string AddressRangeType::print() const {
    std::ostringstream str;
    ccprintf(str, "%s: 0x%lx + 0x%lx", address_range_names[rangeID], base, offset);
    return str.str();
}
const char *const AddressRangeType::address_range_names[7] = {
    "SPD_DATA_CACHEABLE_RANGE",
    "SPD_DATA_NONCACHEABLE_RANGE",
    "SPD_SIZE_RANGE",
    "SPD_READY_RANGE",
    "SCALAR_RANGE",
    "INSTRUCTION_RANGE",
    "MAX"};
} // namespace gem5