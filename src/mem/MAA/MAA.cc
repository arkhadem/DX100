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
#include "debug/MAA.hh"
#include "debug/MAACpuPort.hh"
#include "debug/MAACachePort.hh"
#include "debug/MAAMemPort.hh"
#include "debug/MAAController.hh"
#include "sim/cur_tick.hh"
#include <cassert>
#include <cstdint>
#include <string>

#ifndef TRACING_ON
#define TRACING_ON 1
#endif

template <typename Integral_t>
Integral_t calc_log2(Integral_t val) {
    static_assert(std::is_integral_v<Integral_t>, "Only integral types are allowed for bitwise operations!");

    Integral_t n = 0;
    while ((val >>= 1)) {
        n++;
    }
    return n;
};

namespace gem5 {

MAA::MAAResponsePort::MAAResponsePort(const std::string &_name, MAA &_maa, const std::string &_label)
    : QueuedResponsePort(_name, queue),
      maa{_maa},
      queue(_maa, *this, true, _label) {
}

MAA::MAA(const MAAParams &p)
    : ClockedObject(p),
      addrRanges(p.addr_ranges.begin(), p.addr_ranges.end()),
      num_tiles(p.num_tiles_per_core * p.num_cores),
      num_tile_elements(p.num_tile_elements),
      num_regs(p.num_regs_per_core * p.num_cores),
      num_instructions_per_core(p.num_instructions_per_core),
      num_row_table_rows_per_slice(p.num_row_table_rows_per_slice),
      num_row_table_entries_per_subslice_row(p.num_row_table_entries_per_subslice_row),
      num_row_table_config_cache_entries(p.num_row_table_config_cache_entries),
      reconfigure_row_table(p.reconfigure_row_table),
      reorder_row_table(p.no_reorder == false ? true : false),
      force_cache_access(p.force_cache_access),
      num_initial_row_table_slices(p.num_initial_row_table_slices),
      num_request_table_addresses(p.num_request_table_addresses),
      num_request_table_entries_per_address(p.num_request_table_entries_per_address),
      num_memory_channels(p.num_memory_channels),
      num_cores(p.num_cores),
      num_maas(p.num_maas),
      rowtable_latency(p.rowtable_latency),
      addrRegions(MAX_CMD_REGIONS, {0, 0}),
      maxRegionID(-1),
      system(p.system),
      mmu(p.mmu),
      issueInstructionEvent([this] { issueInstruction(); }, name()),
      dispatchInstructionEvent([this] { dispatchInstruction(); }, name()),
      dispatchRegisterEvent([this] { dispatchRegister(); }, name()),
      stats(this, p.num_maas, this),
      sendCacheEvent([this] { sendOutstandingCachePacket(); }, name()),
      sendMemEvent([this] { sendOutstandingMemPacket(); }, name()) {

    m_core_addr_bits = calc_log2(num_cores);
    panic_if(num_cores % num_maas != 0, "Number of cores %d must be a multiple of the number of MAAs %s\n", num_cores, num_maas);
    num_cores_per_maas = num_cores / num_maas;
    requestorId = p.system->getRequestorId(this);
    spd = new SPD(this, num_tiles, num_tile_elements, p.spd_read_latency, p.spd_write_latency, p.num_spd_read_ports_per_maa * num_maas, p.num_spd_write_ports_per_maa * num_maas);
    rf = new RF(num_regs);
    num_instructions_per_maa = num_instructions_per_core * num_cores_per_maas;
    num_instructions_total = num_instructions_per_maa * num_maas;
    ifile = new IF(num_instructions_per_maa, num_maas, this);
    streamAccessUnits = new StreamAccessUnit[num_maas];
    streamAccessIdle = new bool[num_maas];
    for (int i = 0; i < num_maas; i++) {
        streamAccessUnits[i].allocate(i, num_request_table_addresses, num_request_table_entries_per_address, num_tile_elements, this);
        streamAccessIdle[i] = true;
    }
    indirectAccessUnits = new IndirectAccessUnit[num_maas];
    indirectAccessIdle = new bool[num_maas];
    for (int i = 0; i < num_maas; i++) {
        indirectAccessIdle[i] = true;
    }
    invalidator = new Invalidator();
    invalidator->allocate(num_maas, num_tiles, num_tile_elements, addrRanges.front().start(), this);
    aluUnits = new ALUUnit[num_maas];
    aluUnitsIdle = new bool[num_maas];
    for (int i = 0; i < num_maas; i++) {
        aluUnits[i].allocate(this, i, p.ALU_lane_latency, p.num_ALU_lanes, num_tile_elements);
        aluUnitsIdle[i] = true;
    }
    rangeUnits = new RangeFuserUnit[num_maas];
    rangeUnitsIdle = new bool[num_maas];
    for (int i = 0; i < num_maas; i++) {
        rangeUnits[i].allocate(num_tile_elements, this, i);
        rangeUnitsIdle[i] = true;
    }
    invalidatorIdle = true;
    for (int i = 0; i < p.port_mem_sides_connection_count; ++i) {
        std::string portName = csprintf("%s.mem_side_port[%d]", p.name, i);
        memSidePorts.push_back(new MemSidePort(portName, this, "MemSidePort"));
    }
    for (int i = 0; i < p.port_cache_sides_connection_count; ++i) {
        std::string portName = csprintf("%s.cache_side_port[%d]", p.name, i);
        cacheSidePorts.push_back(new CacheSidePort(portName, this, "CacheSidePort"));
        cacheSidePorts[i]->allocate(i, p.max_outstanding_cache_side_packets);
    }
    panic_if(p.port_cpu_sides_connection_count != num_cores, "Number of CPU ports must be equal to the number of cores");
    for (int i = 0; i < num_cores; ++i) {
        cpuPortAddrRanges.push_back(AddrRangeList());
    }
    int range_id = 0;
    for (AddrRange range : addrRanges) {
        switch (range_id) {
        case (uint8_t)AddressRangeType::Type::SPD_DATA_NONCACHEABLE_RANGE:
        case (uint8_t)AddressRangeType::Type::SPD_DATA_CACHEABLE_RANGE: {
            // Addr start = range.start();
            // Addr size_per_port = range.size() / num_cores;
            // for (int i = 0; i < num_cores; ++i) {
            //     DPRINTF(MAA, "Range[%s] for CPU port[%d]: %lx-%lx\n", AddressRangeType::address_range_names[range_id], i, start, start + size_per_port);
            //     cpuPortAddrRanges[i].push_back(AddrRange(start, start + size_per_port));
            //     start += size_per_port;
            // }
            Addr start = range.start();
            Addr end = range.end();
            std::vector<Addr> mask;
            Addr curr_mask = 1 << 6;
            printf("MAA: cpu masks: ");
            for (int i = 0; i < log2(num_cores); i++) {
                mask.push_back(curr_mask);
                printf("%lx ", curr_mask);
                curr_mask = curr_mask << 1;
            }
            printf("\n");
            for (int i = 0; i < num_cores; ++i) {
                AddrRange curr_range = AddrRange(start, end, mask, i);
                printf("MAA: Range[%s] for CPU port[%d]: %s\n", AddressRangeType::address_range_names[range_id], i, curr_range.to_string().c_str());
                cpuPortAddrRanges[i].push_back(curr_range);
            }
            break;
        }
        default: {
            DPRINTF(MAA, "Range[%s] for CPU port[0]: %lx-%lx\n", AddressRangeType::address_range_names[range_id], range.start(), range.end());
            cpuPortAddrRanges[0].push_back(AddrRange(range.start(), range.end()));
            break;
        }
        }
        range_id++;
    }
    for (int i = 0; i < p.port_cpu_sides_connection_count; ++i) {
        std::string portName = csprintf("%s.cpu_side_port[%d]", p.name, i);
        cpuSidePorts.push_back(new CpuSidePort(portName, *this, "CpuSidePort"));
        cpuSidePorts[i]->allocate(i, p.max_outstanding_cpu_side_packets);
    }

    my_last_idle_tick = curTick();
    my_last_reset_tick = curTick();
    my_num_outstanding_indirect_pkts = new uint32_t[num_maas];
    my_num_outstanding_stream_pkts = new uint32_t[num_maas];
    for (int i = 0; i < num_maas; i++) {
        my_num_outstanding_indirect_pkts[i] = 0;
        my_num_outstanding_stream_pkts[i] = 0;
    }
}

void MAA::init() {
    for (auto port : cpuSidePorts) {
        if (!port->isConnected())
            fatal("Cache ports on %s are not connected\n", name());
        port->sendRangeChange();
    }
}

MAA::~MAA() {
    for (auto port : memSidePorts)
        delete port;
    for (auto port : cacheSidePorts)
        delete port;
    for (auto port : cpuSidePorts)
        delete port;
    delete[] my_num_outstanding_indirect_pkts;
    delete[] my_num_outstanding_stream_pkts;
}

void MAA::addAddrRegion(Addr start, Addr end, int8_t id) {
    panic_if(id >= MAX_CMD_REGIONS, "Region ID %d exceeds the maximum number of regions %d\n", id, MAX_CMD_REGIONS);
    panic_if(start >= end, "Region ID %d start address 0x%x >= end address 0x%x\n", id, start, end);
    panic_if(end == 0, "Region ID %d end address 0x%x is invalid\n", id, end);
    maxRegionID = -1;
    for (int i = 0; i < MAX_CMD_REGIONS; i++) {
        if (start <= addrRegions[i].first && addrRegions[i].first < end) {
            panic("Region[%d]:[0x%x-0x%x] overlaps with new Region[%d]:[0x%x-0x%x]\n", i, addrRegions[i].first, addrRegions[i].second, id, start, end);
        } else {
            maxRegionID = i;
        }
    }
    DPRINTF(MAA, "Region[%d]:[0x%x-0x%x] added\n", id, start, end);
    addrRegions[id] = {start, end};
    if (id > maxRegionID) {
        maxRegionID = id;
    }
}

void MAA::clearAddrRegion() {
    DPRINTF(MAA, "all addr regions cleared\n");
    maxRegionID = -1;
    for (int i = 0; i < MAX_CMD_REGIONS; i++) {
        addrRegions[i] = {0, 0};
    }
}

int MAA::getAddrRegion(Addr addr) {
    int reg_id = -1;
    for (int reg_idx = 0; reg_idx <= maxRegionID; reg_idx++) {
        if (addrRegions[reg_idx].first == 0 && addrRegions[reg_idx].second == 0) {
            continue;
        } else if (addrRegions[reg_idx].first <= addr && addr < addrRegions[reg_idx].second) {
            reg_id = reg_idx;
            break;
        }
    }
    panic_if(reg_id == -1, "Address 0x%x does not belong to any region\n", addr);
    return reg_id;
}

Port &MAA::getPort(const std::string &if_name, PortID idx) {
    if (if_name == "mem_sides" && idx < memSidePorts.size()) {
        return *memSidePorts[idx];
    } else if (if_name == "cpu_sides" && idx < cpuSidePorts.size()) {
        return *cpuSidePorts[idx];
    } else if (if_name == "cache_sides" && idx < cacheSidePorts.size()) {
        return *cacheSidePorts[idx];
    } else {
        return ClockedObject::getPort(if_name, idx);
    }
}
int MAA::inRange(Addr addr) const {
    int r_id = -1;
    for (const auto &r : addrRanges) {
        if (r.contains(addr)) {
            break;
        }
    }
    return r_id;
}
void MAA::addRamulator(memory::Ramulator2 *_ramulator2) {
    _ramulator2->getAddrMapData(m_org,
                                m_addr_bits,
                                m_num_levels,
                                m_tx_offset,
                                m_col_bits_idx,
                                m_row_bits_idx);
    DPRINTF(MAA, "DRAM organization [n_levels: %d] -- CH: %d, RA: %d, BG: %d, BA: %d, RO: %d, CO: %d\n",
            m_num_levels,
            m_org[ADDR_CHANNEL_LEVEL],
            m_org[ADDR_RANK_LEVEL],
            m_org[ADDR_BANKGROUP_LEVEL],
            m_org[ADDR_BANK_LEVEL],
            m_org[ADDR_ROW_LEVEL],
            m_org[ADDR_COLUMN_LEVEL]);
    DPRINTF(MAA, "DRAM addr_bit -- RO: %d, BA: %d, BG: %d, RA: %d, CO: %d, CH: %d, TX: %d\n",
            m_addr_bits[ADDR_ROW_LEVEL],
            m_addr_bits[ADDR_BANK_LEVEL],
            m_addr_bits[ADDR_BANKGROUP_LEVEL],
            m_addr_bits[ADDR_RANK_LEVEL],
            m_addr_bits[ADDR_COLUMN_LEVEL],
            m_addr_bits[ADDR_CHANNEL_LEVEL],
            m_tx_offset);
    assert(m_num_levels == 6);
    num_channels = m_org[ADDR_CHANNEL_LEVEL];
    panic_if(memSidePorts.size() != num_channels, "Number of memory channels %d != number of memside ports %d\n", num_channels, memSidePorts.size());
    mem_channels_blocked = new bool[num_channels];
    for (int i = 0; i < num_channels; i++) {
        mem_channels_blocked[i] = false;
    }
    panic_if(cacheSidePorts.size() != num_cores, "Number of cores %d != number of cacheside ports %d\n", num_cores, cacheSidePorts.size());
    cache_bus_blocked = new bool[num_cores];
    for (int i = 0; i < num_cores; i++) {
        cache_bus_blocked[i] = false;
    }
    for (int i = 0; i < memSidePorts.size(); i++) {
        memSidePorts[i]->allocate(i);
    }
    for (int i = 0; i < num_maas; i++) {
        indirectAccessUnits[i].allocate(i, num_tile_elements, num_row_table_rows_per_slice,
                                        num_row_table_entries_per_subslice_row,
                                        num_row_table_config_cache_entries,
                                        reconfigure_row_table,
                                        reorder_row_table,
                                        num_initial_row_table_slices,
                                        rowtable_latency,
                                        num_channels,
                                        num_cores,
                                        this);
    }
    my_outstanding_indirect_cache_read_pkts = new std::multiset<OutstandingPacket, CompareByTick>[num_cores];
    my_outstanding_indirect_cache_write_pkts = new std::multiset<OutstandingPacket, CompareByTick>[num_cores];
    my_outstanding_indirect_mem_write_pkts = new std::multiset<OutstandingPacket, CompareByTick>[num_channels];
    my_outstanding_indirect_mem_read_pkts = new std::multiset<OutstandingPacket, CompareByTick>[num_channels];
    my_outstanding_stream_cache_read_pkts = new std::multiset<OutstandingPacket, CompareByTick>[num_cores];
    my_outstanding_stream_cache_write_pkts = new std::multiset<OutstandingPacket, CompareByTick>[num_cores];
    my_outstanding_stream_mem_write_pkts = new std::multiset<OutstandingPacket, CompareByTick>[num_cores];
    my_outstanding_stream_mem_read_pkts = new std::multiset<OutstandingPacket, CompareByTick>[num_cores];
}
// RoBaRaCoCh address mapping taking from the Ramulator2
int slice_lower_bits(uint64_t &addr, int bits) {
    int lbits = addr & ((1 << bits) - 1);
    addr >>= bits;
    return lbits;
}
std::vector<int> MAA::map_addr(Addr addr) {
    std::vector<int> addr_vec(m_num_levels, -1);
    addr = addr >> m_tx_offset;
    addr_vec[0] = slice_lower_bits(addr, m_addr_bits[0]);
    addr_vec[m_addr_bits.size() - 1] = slice_lower_bits(addr, m_addr_bits[m_addr_bits.size() - 1]);
    for (int i = 1; i <= m_row_bits_idx; i++) {
        addr_vec[i] = slice_lower_bits(addr, m_addr_bits[i]);
    }
    return addr_vec;
}
int MAA::channel_addr(Addr addr) {
    addr = addr >> m_tx_offset;
    return slice_lower_bits(addr, m_addr_bits[0]);
}
int MAA::core_addr(Addr addr) {
    addr = addr >> m_tx_offset;
    return slice_lower_bits(addr, m_core_addr_bits);
}
bool MAA::allFuncUnitsIdle() {
    if (invalidator->getState() != Invalidator::Status::Idle) {
        return false;
    }
    for (int i = 0; i < num_maas; i++) {
        if (streamAccessUnits[i].getState() != StreamAccessUnit::Status::Idle) {
            return false;
        }
        if (indirectAccessUnits[i].getState() != IndirectAccessUnit::Status::Idle) {
            return false;
        }
        if (aluUnits[i].getState() != ALUUnit::Status::Idle) {
            return false;
        }
        if (rangeUnits[i].getState() != RangeFuserUnit::Status::Idle) {
            return false;
        }
    }
    return true;
}
bool MAA::getAddrRegionPermit(Instruction *instruction) {
    return invalidator->getAddrRegionPermit(instruction);
}
void MAA::issueInstruction() {
    bool were_all_units_idle = allFuncUnitsIdle();
    bool are_all_units_idle = were_all_units_idle;
    bool issued = true;
    int num_issued = 0;
    while (issued) {
        issued = false;
        if (invalidatorIdle) {
            panic_if(invalidator->getState() != Invalidator::Status::Idle, "Invalidator is not idle!\n");
            Instruction *inst = ifile->getReady(FuncUnitType::INVALIDATOR);
            if (inst != nullptr) {
                invalidator->setInstruction(inst);
                invalidator->scheduleExecuteInstructionEvent(num_issued++);
                are_all_units_idle = false;
                issued = true;
                invalidatorIdle = false;
            }
        }
        int func_unit_type_base = rand() % 4;
        for (int func_unit_type_offset = 0; func_unit_type_offset < 4; func_unit_type_offset++) {
            int func_unit_type = (func_unit_type_base + func_unit_type_offset) % 4;
            int maa_id_base = rand() % num_maas;
            for (int maa_id_offset = 0; maa_id_offset < num_maas; maa_id_offset++) {
                int maa_id = (maa_id_base + maa_id_offset) % num_maas;
                switch (func_unit_type) {
                case 0: {
                    if (streamAccessIdle[maa_id]) {
                        panic_if(streamAccessUnits[maa_id].getState() != StreamAccessUnit::Status::Idle, "StreamAccessUnit[%d] is not idle!\n", maa_id);
                        Instruction *inst = ifile->getReady(FuncUnitType::STREAM, maa_id);
                        if (inst != nullptr) {
                            if (inst->dst1SpdID != -1) {
                                spd->setTileService(inst->dst1SpdID, inst->getWordSize(inst->dst1SpdID));
                            }
                            streamAccessUnits[maa_id].setInstruction(inst);
                            streamAccessUnits[maa_id].scheduleExecuteInstructionEvent(num_issued++);
                            streamAccessIdle[maa_id] = false;
                            are_all_units_idle = false;
                            issued = true;
                        }
                    }
                    break;
                }
                case 1: {
                    if (indirectAccessIdle[maa_id]) {
                        panic_if(indirectAccessUnits[maa_id].getState() != IndirectAccessUnit::Status::Idle, "IndirectAccessUnit[%d] is not idle!\n", maa_id);
                        Instruction *inst = ifile->getReady(FuncUnitType::INDIRECT, maa_id);
                        if (inst != nullptr) {
                            if (inst->dst1SpdID != -1) {
                                spd->setTileService(inst->dst1SpdID, inst->getWordSize(inst->dst1SpdID));
                            }
                            indirectAccessUnits[maa_id].setInstruction(inst);
                            indirectAccessUnits[maa_id].scheduleExecuteInstructionEvent(num_issued++);
                            indirectAccessIdle[maa_id] = false;
                            are_all_units_idle = false;
                            issued = true;
                        }
                    }
                    break;
                }
                case 2: {
                    if (aluUnitsIdle[maa_id]) {
                        panic_if(aluUnits[maa_id].getState() != ALUUnit::Status::Idle, "ALUUnit[%d] is not idle!\n", maa_id);
                        Instruction *inst = ifile->getReady(FuncUnitType::ALU, maa_id);
                        if (inst != nullptr) {
                            if (inst->dst1SpdID != -1) {
                                spd->setTileService(inst->dst1SpdID, inst->getWordSize(inst->dst1SpdID));
                            }
                            aluUnits[maa_id].setInstruction(inst);
                            aluUnits[maa_id].scheduleExecuteInstructionEvent(num_issued++);
                            aluUnitsIdle[maa_id] = false;
                            are_all_units_idle = false;
                            issued = true;
                        } else {
                            break;
                        }
                    }
                    break;
                }
                case 3: {
                    if (rangeUnitsIdle[maa_id]) {
                        panic_if(rangeUnits[maa_id].getState() != RangeFuserUnit::Status::Idle, "RangeFuserUnit[%d] is not idle!\n", maa_id);
                        Instruction *inst = ifile->getReady(FuncUnitType::RANGE, maa_id);
                        if (inst != nullptr) {
                            if (inst->dst1SpdID != -1) {
                                spd->setTileService(inst->dst1SpdID, inst->getWordSize(inst->dst1SpdID));
                            }
                            if (inst->dst2SpdID != -1) {
                                spd->setTileService(inst->dst2SpdID, inst->getWordSize(inst->dst1SpdID));
                            }
                            rangeUnits[maa_id].setInstruction(inst);
                            rangeUnits[maa_id].scheduleExecuteInstructionEvent(num_issued++);
                            rangeUnitsIdle[maa_id] = false;
                            are_all_units_idle = false;
                            issued = true;
                        }
                    }
                    break;
                }
                default:
                    panic("Invalid func_unit_type %d\n", func_unit_type);
                }
            }
        }
    }
    if (were_all_units_idle && !are_all_units_idle) {
        stats.cycles_IDLE += getTicksToCycles(curTick() - my_last_idle_tick);
    }
}
uint8_t MAA::getTileStatus(InstructionPtr instruction, int tile_id, bool is_dst) {
    if (tile_id == -1)
        return (uint8_t)(Instruction::TileStatus::Finished);

    bool is_dirty = spd->getTileDirty(tile_id);
    SPD::TileStatus status = spd->getTileStatus(tile_id);
    if (instruction->getWordSize(tile_id) == 8) {
        if (spd->getTileDirty(tile_id + 1) == true) {
            is_dirty = true;
        }
        panic_if(spd->getTileStatus(tile_id + 1) != status, "Tile[%d] and Tile[%d] have different statuses %s != %s\n",
                 tile_id, tile_id + 1,
                 spd->tile_status_names[(uint8_t)(spd->getTileStatus(tile_id))],
                 spd->tile_status_names[(uint8_t)(spd->getTileStatus(tile_id + 1))]);
    }
    if (is_dirty) {
        return (uint8_t)(Instruction::TileStatus::WaitForInvalidation);
    }

    if (is_dst) {
        return (uint8_t)(Instruction::TileStatus::WaitForService);
    } else {
        if (status == SPD::TileStatus::Idle) {
            return (uint8_t)(Instruction::TileStatus::WaitForService);
        } else if (status == SPD::TileStatus::Service) {
            return (uint8_t)(Instruction::TileStatus::Service);
        } else if (status == SPD::TileStatus::Finished) {
            return (uint8_t)(Instruction::TileStatus::Finished);
        } else {
            assert(false);
        }
    }
    assert(false);
    return (uint8_t)(Instruction::TileStatus::WaitForService);
}
void MAA::dispatchRegister() {
    DPRINTF(MAAController, "%s: dispatching register...!\n", __func__);
    assert(my_register_pkts.size() == my_registers.size());
    auto pkt_it = my_register_pkts.begin();
    auto register_it = my_registers.begin();
    while (pkt_it != my_register_pkts.end() && register_it != my_registers.end()) {
        RegisterPtr reg = *register_it;
        PacketPtr pkt = *pkt_it;
        if (ifile->canPushRegister(*reg)) {
            DPRINTF(MAAController, "%s: register %d write dispatched!\n", __func__, reg->register_id);
            if (reg->size == 4) {
                rf->setData<uint32_t>(reg->register_id, reg->data_UINT32);
            } else {
                panic_if(reg->size != 8, "Invalid size for RF data: %d\n", reg->size);
                rf->setData<uint64_t>(reg->register_id, reg->data_UINT64);
            }
            pkt->makeTimingResponse();
            pkt->headerDelay = pkt->payloadDelay = 0;
            cpuSidePorts[0]->schedTimingResp(pkt, getClockEdge(Cycles(1)));
            pkt_it = my_register_pkts.erase(pkt_it);
            register_it = my_registers.erase(register_it);
            delete reg;
        } else {
            DPRINTF(MAAController, "%s: Reg write %d failed to dipatch!\n", __func__, reg->register_id);
            pkt_it++;
            register_it++;
        }
    }
}
void MAA::dispatchInstruction() {
    DPRINTF(MAAController, "%s: dispatching instruction...!\n", __func__);
    assert(my_instruction_pkts.size() == my_instructions.size());
    assert(my_instruction_recvs.size() == my_instructions.size());
    assert(my_instruction_RIDs.size() == my_instructions.size());
    auto pkt_it = my_instruction_pkts.begin();
    auto recv_it = my_instruction_recvs.begin();
    auto rid_it = my_instruction_RIDs.begin();
    auto instruction_it = my_instructions.begin();
    while (pkt_it != my_instruction_pkts.end() && instruction_it != my_instructions.end() &&
           recv_it != my_instruction_recvs.end() && rid_it != my_instruction_RIDs.end()) {
        if (*recv_it == true) {
            InstructionPtr instruction = *instruction_it;
            PacketPtr pkt = *pkt_it;
            instruction->src1Status = (Instruction::TileStatus)getTileStatus(instruction, instruction->src1SpdID, false);
            instruction->src2Status = (Instruction::TileStatus)getTileStatus(instruction, instruction->src2SpdID, false);
            instruction->condStatus = (Instruction::TileStatus)getTileStatus(instruction, instruction->condSpdID, false);
            // assume that we can read from any tile, so invalidate all destinations
            // Instructions with DST1: stream and indirect load, range loop, ALU
            instruction->dst1Status = (Instruction::TileStatus)getTileStatus(instruction, instruction->dst1SpdID, true);
            // Instructions with DST2: range loop
            instruction->dst2Status = (Instruction::TileStatus)getTileStatus(instruction, instruction->dst2SpdID, true);
            if (ifile->pushInstruction(*instruction)) {
                DPRINTF(MAAController, "%s: %s dispatched!\n", __func__, instruction->print());
                if (instruction->dst1SpdID != -1) {
                    assert(instruction->dst1SpdID != instruction->src1SpdID);
                    assert(instruction->dst1SpdID != instruction->src2SpdID);
                    spd->setTileIdle(instruction->dst1SpdID, instruction->getWordSize(instruction->dst1SpdID));
                    spd->setTileNotReady(instruction->dst1SpdID, instruction->getWordSize(instruction->dst1SpdID));
                }
                if (instruction->dst2SpdID != -1) {
                    assert(instruction->dst2SpdID != instruction->src1SpdID);
                    assert(instruction->dst2SpdID != instruction->src2SpdID);
                    spd->setTileIdle(instruction->dst2SpdID, instruction->getWordSize(instruction->dst2SpdID));
                    spd->setTileNotReady(instruction->dst2SpdID, instruction->getWordSize(instruction->dst2SpdID));
                }
                if (instruction->src1SpdID != -1) {
                    spd->setTileNotReady(instruction->src1SpdID, instruction->getWordSize(instruction->src1SpdID));
                }
                if (instruction->src2SpdID != -1) {
                    spd->setTileNotReady(instruction->src2SpdID, instruction->getWordSize(instruction->src2SpdID));
                }
                pkt->makeTimingResponse();
                pkt->headerDelay = pkt->payloadDelay = 0;
                cpuSidePorts[0]->schedTimingResp(pkt, getClockEdge(Cycles(1)));
                scheduleIssueInstructionEvent(1);
                pkt_it = my_instruction_pkts.erase(pkt_it);
                recv_it = my_instruction_recvs.erase(recv_it);
                rid_it = my_instruction_RIDs.erase(rid_it);
                instruction_it = my_instructions.erase(instruction_it);
                delete instruction;
            } else {
                DPRINTF(MAAController, "%s: %s failed to dipatch!\n", __func__, instruction->print());
                pkt_it++;
                recv_it++;
                rid_it++;
                instruction_it++;
            }
        } else {
            pkt_it++;
            recv_it++;
            rid_it++;
            instruction_it++;
        }
    }
}
void MAA::finishInstructionCompute(Instruction *instruction) {
    DPRINTF(MAAController, "%s: %s finishing!\n", __func__, instruction->print());
    if (instruction->dst1SpdID != -1) {
        spd->setTileFinished(instruction->dst1SpdID, instruction->getWordSize(instruction->dst1SpdID));
        setTileReady(instruction->dst1SpdID, instruction->getWordSize(instruction->dst1SpdID));
    }
    if (instruction->dst2SpdID != -1) {
        spd->setTileFinished(instruction->dst2SpdID, instruction->getWordSize(instruction->dst2SpdID));
        setTileReady(instruction->dst2SpdID, instruction->getWordSize(instruction->dst2SpdID));
    }
    if (instruction->src1SpdID != -1) {
        setTileReady(instruction->src1SpdID, instruction->getWordSize(instruction->src1SpdID));
    }
    if (instruction->src2SpdID != -1) {
        setTileReady(instruction->src2SpdID, instruction->getWordSize(instruction->src2SpdID));
    }
    ifile->finishInstructionCompute(instruction);
    if (num_maas > 1)
        invalidator->finishInstruction(instruction);
    switch (instruction->funcUniType) {
    case FuncUnitType::STREAM: {
        streamAccessIdle[instruction->maa_id] = true;
        break;
    }
    case FuncUnitType::INDIRECT: {
        indirectAccessIdle[instruction->maa_id] = true;
        break;
    }
    case FuncUnitType::ALU: {
        aluUnitsIdle[instruction->maa_id] = true;
        break;
    }
    case FuncUnitType::RANGE: {
        rangeUnitsIdle[instruction->maa_id] = true;
        break;
    }
    default: {
        assert(false);
    }
    }
    scheduleIssueInstructionEvent();
    scheduleDispatchInstructionEvent();
    scheduleDispatchRegisterEvent();
    if (allFuncUnitsIdle()) {
        my_last_idle_tick = curTick();
    }
}
void MAA::setTileReady(int tileID, int wordSize) {
    DPRINTF(MAAController, "%s: tile[%d] is ready!\n", __func__, tileID);
    spd->setTileReady(tileID, wordSize);
    if (spd->getTileReady(tileID)) {
        assert(my_ready_pkts.size() == my_ready_tile_ids.size());
        auto pkt_it = my_ready_pkts.begin();
        auto tile_id_it = my_ready_tile_ids.begin();
        while (pkt_it != my_ready_pkts.end() && tile_id_it != my_ready_tile_ids.end()) {
            bool is_ready = (*tile_id_it == tileID);
            if (wordSize == 8) {
                is_ready = is_ready || (*tile_id_it == tileID + 1);
            }
            if (is_ready) {
                PacketPtr pkt = *pkt_it;
                DPRINTF(MAAController, "%s: responding to outstanding ready packet %s!\n", __func__, pkt->print());
                pkt->makeTimingResponse();
                pkt->headerDelay = pkt->payloadDelay = 0;
                cpuSidePorts[0]->schedTimingResp(pkt, getClockEdge(Cycles(1)));
                pkt_it = my_ready_pkts.erase(pkt_it);
                tile_id_it = my_ready_tile_ids.erase(tile_id_it);
            } else {
                pkt_it++;
                tile_id_it++;
            }
        }
    }
}
void MAA::finishInstructionInvalidate(Instruction *instruction, int tileID) {
    invalidatorIdle = true;
    spd->setTileClean(tileID, instruction->getWordSize(tileID));
    ifile->finishInstructionInvalidate(instruction, tileID, (uint8_t)(spd->getTileStatus(tileID)));
    scheduleIssueInstructionEvent();
    if (allFuncUnitsIdle()) {
        my_last_idle_tick = curTick();
    }
}
void MAA::scheduleIssueInstructionEvent(int latency) {
    DPRINTF(MAAController, "%s: scheduling issue for the next %d cycles!\n", __func__, latency);
    Tick new_when = curTick() + latency;
    if (!issueInstructionEvent.scheduled()) {
        schedule(issueInstructionEvent, new_when);
    } else {
        Tick old_when = issueInstructionEvent.when();
        if (new_when < old_when)
            reschedule(issueInstructionEvent, new_when);
    }
}
void MAA::scheduleDispatchInstructionEvent(int latency) {
    DPRINTF(MAAController, "%s: scheduling instruction dispatch for the next %d cycles!\n", __func__, latency);
    Tick new_when = curTick() + latency;
    if (!dispatchInstructionEvent.scheduled()) {
        schedule(dispatchInstructionEvent, new_when);
    } else {
        Tick old_when = dispatchInstructionEvent.when();
        if (new_when < old_when)
            reschedule(dispatchInstructionEvent, new_when);
    }
}
void MAA::scheduleDispatchRegisterEvent(int latency) {
    DPRINTF(MAAController, "%s: scheduling register dispatch for the next %d cycles!\n", __func__, latency);
    Tick new_when = curTick() + latency;
    if (!dispatchRegisterEvent.scheduled()) {
        schedule(dispatchRegisterEvent, new_when);
    } else {
        Tick old_when = dispatchRegisterEvent.when();
        if (new_when < old_when)
            reschedule(dispatchRegisterEvent, new_when);
    }
}
Tick MAA::getClockEdge(Cycles cycles) const {
    return clockEdge(cycles);
}
Cycles MAA::getTicksToCycles(Tick t) const {
    return ticksToCycles(t);
}
Tick MAA::getCyclesToTicks(Cycles c) const {
    return cyclesToTicks(c);
}
void MAA::resetStats() {
    my_last_idle_tick = curTick();
    my_last_reset_tick = curTick();
    printf("Resetting MAA stats\n");
    ClockedObject::resetStats();
    printf("NumInst after reset: %lf\n", stats.numInst.value());
}

#define MAKE_INDIRECT_STAT_NAME(name) \
    (std::string("I") + std::to_string(indirect_id) + "_" + std::string(name)).c_str()

#define MAKE_STREAM_STAT_NAME(name) \
    (std::string("S") + std::to_string(stream_id) + "_" + std::string(name)).c_str()

#define MAKE_RANGE_STAT_NAME(name) \
    (std::string("A") + std::to_string(range_id) + "_" + std::string(name)).c_str()

#define MAKE_ALU_STAT_NAME(name) \
    (std::string("A") + std::to_string(alu_id) + "_" + std::string(name)).c_str()

#define MAKE_INVALIDATOR_STAT_NAME(name) \
    (std::string("INV_") + std::string(name)).c_str()

Tick MAA::getCurTick() {
    return curTick();
}

void MAA::MAAStats::preDumpStats() {
    statistics::Group::preDumpStats();

    cycles_TOTAL = maa->getTicksToCycles(maa->getCurTick() - maa->my_last_reset_tick);
    if (maa->allFuncUnitsIdle())
        cycles_IDLE += maa->getTicksToCycles(maa->getCurTick() - maa->my_last_idle_tick);
}
MAA::MAAStats::MAAStats(statistics::Group *parent, int num_maas, MAA *_maa)
    : statistics::Group(parent),
      maa(_maa),
      ADD_STAT(numInst_INDRD, statistics::units::Count::get(), "number of indirect read instructions"),
      ADD_STAT(numInst_INDWR, statistics::units::Count::get(), "number of indirect write instructions"),
      ADD_STAT(numInst_INDRMW, statistics::units::Count::get(), "number of indirect read-modify-write instructions"),
      ADD_STAT(numInst_STRRD, statistics::units::Count::get(), "number of stream read instructions"),
      ADD_STAT(numInst_STRWR, statistics::units::Count::get(), "number of stream write instructions"),
      ADD_STAT(numInst_RANGE, statistics::units::Count::get(), "number of range loop instructions"),
      ADD_STAT(numInst_ALUS, statistics::units::Count::get(), "number of ALU Scalar instructions"),
      ADD_STAT(numInst_ALUV, statistics::units::Count::get(), "number of ALU Vector instructions"),
      ADD_STAT(numInst_ALUR, statistics::units::Count::get(), "number of ALU Reduction instructions"),
      ADD_STAT(numInst_INV, statistics::units::Count::get(), "number of Invalidation for instructions"),
      ADD_STAT(numInst, statistics::units::Count::get(), "total number of instructions"),
      ADD_STAT(cycles_INDRD, statistics::units::Count::get(), "number of indirect read instruction cycles"),
      ADD_STAT(cycles_INDWR, statistics::units::Count::get(), "number of indirect write instruction cycles"),
      ADD_STAT(cycles_INDRMW, statistics::units::Count::get(), "number of indirect read-modify-write instruction cycles"),
      ADD_STAT(cycles_STRRD, statistics::units::Count::get(), "number of stream read instruction cycles"),
      ADD_STAT(cycles_STRWR, statistics::units::Count::get(), "number of stream write instruction cycles"),
      ADD_STAT(cycles_RANGE, statistics::units::Count::get(), "number of range loop instruction cycles"),
      ADD_STAT(cycles_ALUS, statistics::units::Count::get(), "number of ALU Scalar instruction cycles"),
      ADD_STAT(cycles_ALUV, statistics::units::Count::get(), "number of ALU Vector instruction cycles"),
      ADD_STAT(cycles_ALUR, statistics::units::Count::get(), "number of ALU Reduction instruction cycles"),
      ADD_STAT(cycles_INV, statistics::units::Count::get(), "number of Invalidation for instruction cycles"),
      ADD_STAT(cycles_IDLE, statistics::units::Count::get(), "number of idle cycles"),
      ADD_STAT(cycles_BUSY, statistics::units::Count::get(), "number of busy cycles"),
      ADD_STAT(cycles_TOTAL, statistics::units::Count::get(), "number of total cycles"),
      ADD_STAT(cycles, statistics::units::Count::get(), "total number of instruction cycles"),
      ADD_STAT(avgCPI_INDRD, statistics::units::Count::get(), "average CPI for indirect read instructions"),
      ADD_STAT(avgCPI_INDWR, statistics::units::Count::get(), "average CPI for indirect write instructions"),
      ADD_STAT(avgCPI_INDRMW, statistics::units::Count::get(), "average CPI for indirect read-modify-write instructions"),
      ADD_STAT(avgCPI_STRRD, statistics::units::Count::get(), "average CPI for stream read instructions"),
      ADD_STAT(avgCPI_STRWR, statistics::units::Count::get(), "average CPI for stream write instructions"),
      ADD_STAT(avgCPI_RANGE, statistics::units::Count::get(), "average CPI for range loop instructions"),
      ADD_STAT(avgCPI_ALUS, statistics::units::Count::get(), "average CPI for ALU Scalar instructions"),
      ADD_STAT(avgCPI_ALUV, statistics::units::Count::get(), "average CPI for ALU Vector instructions"),
      ADD_STAT(avgCPI_ALUR, statistics::units::Count::get(), "average CPI for ALU Reduction instructions"),
      ADD_STAT(avgCPI_INV, statistics::units::Count::get(), "average CPI for Invalidation for instructions"),
      ADD_STAT(avgCPI, statistics::units::Count::get(), "average CPI for all instructions"),
      ADD_STAT(port_cache_WR_packets, statistics::units::Count::get(), "number of cache write packets"),
      ADD_STAT(port_cache_RD_packets, statistics::units::Count::get(), "number of cache read packets"),
      ADD_STAT(port_mem_WR_packets, statistics::units::Count::get(), "number of memory write packets"),
      ADD_STAT(port_mem_RD_packets, statistics::units::Count::get(), "number of memory read packets"),
      ADD_STAT(port_cache_packets, statistics::units::Count::get(), "number of cache packets"),
      ADD_STAT(port_mem_packets, statistics::units::Count::get(), "number of memory packets"),
      ADD_STAT(port_cache_WR_BW, statistics::units::Count::get(), "cache write bandwidth (GB/s)"),
      ADD_STAT(port_cache_RD_BW, statistics::units::Count::get(), "cache read bandwidth (GB/s)"),
      ADD_STAT(port_cache_BW, statistics::units::Count::get(), "cache total bandwidth (GB/s)"),
      ADD_STAT(port_mem_WR_BW, statistics::units::Count::get(), "memory write bandwidth (GB/s)"),
      ADD_STAT(port_mem_RD_BW, statistics::units::Count::get(), "memory read bandwidth (GB/s)"),
      ADD_STAT(port_mem_BW, statistics::units::Count::get(), "memory total bandwidth (GB/s)") {

    numInst_INDRD.flags(statistics::nozero);
    numInst_INDWR.flags(statistics::nozero);
    numInst_INDRMW.flags(statistics::nozero);
    numInst_STRRD.flags(statistics::nozero);
    numInst_STRWR.flags(statistics::nozero);
    numInst_RANGE.flags(statistics::nozero);
    numInst_ALUS.flags(statistics::nozero);
    numInst_ALUV.flags(statistics::nozero);
    numInst_ALUR.flags(statistics::nozero);
    numInst_INV.flags(statistics::nozero);
    numInst.flags(statistics::nozero);
    cycles_INDRD.flags(statistics::nozero);
    cycles_INDWR.flags(statistics::nozero);
    cycles_INDRMW.flags(statistics::nozero);
    cycles_STRRD.flags(statistics::nozero);
    cycles_STRWR.flags(statistics::nozero);
    cycles_RANGE.flags(statistics::nozero);
    cycles_ALUS.flags(statistics::nozero);
    cycles_ALUV.flags(statistics::nozero);
    cycles_ALUR.flags(statistics::nozero);
    cycles_INV.flags(statistics::nozero);
    cycles_IDLE.flags(statistics::nozero);
    cycles_TOTAL.flags(statistics::nozero);
    cycles.flags(statistics::nozero);
    port_cache_WR_packets.flags(statistics::nozero);
    port_cache_RD_packets.flags(statistics::nozero);
    port_mem_WR_packets.flags(statistics::nozero);
    port_mem_RD_packets.flags(statistics::nozero);

    cycles_BUSY = cycles_TOTAL - cycles_IDLE;
    avgCPI_INDRD = cycles_INDRD / numInst_INDRD;
    avgCPI_INDWR = cycles_INDWR / numInst_INDWR;
    avgCPI_INDRMW = cycles_INDRMW / numInst_INDRMW;
    avgCPI_STRRD = cycles_STRRD / numInst_STRRD;
    avgCPI_STRWR = cycles_STRWR / numInst_STRWR;
    avgCPI_RANGE = cycles_RANGE / numInst_RANGE;
    avgCPI_ALUS = cycles_ALUS / numInst_ALUS;
    avgCPI_ALUV = cycles_ALUV / numInst_ALUV;
    avgCPI_ALUR = cycles_ALUR / numInst_ALUR;
    avgCPI_INV = cycles_INV / numInst_INV;
    avgCPI = cycles_TOTAL / numInst;
    port_cache_packets = port_cache_WR_packets + port_cache_RD_packets;
    port_mem_packets = port_mem_WR_packets + port_mem_RD_packets;
    port_cache_WR_BW = port_cache_WR_packets * 64 / (cycles_TOTAL / 3.2);
    port_cache_RD_BW = port_cache_RD_packets * 64 / (cycles_TOTAL / 3.2);
    port_cache_BW = port_cache_packets * 64 / (cycles_TOTAL / 3.2);
    port_mem_WR_BW = port_mem_WR_packets * 64 / (cycles_TOTAL / 3.2);
    port_mem_RD_BW = port_mem_RD_packets * 64 / (cycles_TOTAL / 3.2);
    port_mem_BW = port_mem_packets * 64 / (cycles_TOTAL / 3.2);

    cycles_BUSY.flags(statistics::nonan | statistics::nozero);
    avgCPI_INDRD.flags(statistics::nonan | statistics::nozero);
    avgCPI_INDWR.flags(statistics::nonan | statistics::nozero);
    avgCPI_INDRMW.flags(statistics::nonan | statistics::nozero);
    avgCPI_STRRD.flags(statistics::nonan | statistics::nozero);
    avgCPI_STRWR.flags(statistics::nonan | statistics::nozero);
    avgCPI_RANGE.flags(statistics::nonan | statistics::nozero);
    avgCPI_ALUS.flags(statistics::nonan | statistics::nozero);
    avgCPI_ALUV.flags(statistics::nonan | statistics::nozero);
    avgCPI_ALUR.flags(statistics::nonan | statistics::nozero);
    avgCPI_INV.flags(statistics::nonan | statistics::nozero);
    avgCPI.flags(statistics::nonan | statistics::nozero);
    port_cache_WR_BW.flags(statistics::nonan | statistics::nozero);
    port_cache_RD_BW.flags(statistics::nonan | statistics::nozero);
    port_cache_BW.flags(statistics::nonan | statistics::nozero);
    port_mem_WR_BW.flags(statistics::nonan | statistics::nozero);
    port_mem_RD_BW.flags(statistics::nonan | statistics::nozero);
    port_mem_BW.flags(statistics::nonan | statistics::nozero);

    for (int indirect_id = 0; indirect_id < num_maas; indirect_id++) {
        IND_NumInsts.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_NumInsts"), statistics::units::Count::get(), "number of instructions"));
        IND_NumWordsInserted.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_NumWordsInserted"), statistics::units::Count::get(), "number of words inserted to the row table"));
        IND_NumCacheLineInserted.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_NumCacheLineInserted"), statistics::units::Count::get(), "number of cachelines inserted to the row table"));
        IND_NumRowsInserted.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_NumRowsInserted"), statistics::units::Count::get(), "number of rows inserted to the row table"));
        IND_NumUniqueWordsInserted.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_NumUniqueWordsInserted"), statistics::units::Count::get(), "number of unique words inserted to the row table"));
        IND_NumUniqueCacheLineInserted.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_NumUniqueCacheLineInserted"), statistics::units::Count::get(), "number of unique cachelines inserted to the row table"));
        IND_NumUniqueRowsInserted.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_NumUniqueRowsInserted"), statistics::units::Count::get(), "number of unique rows inserted to the row table"));
        IND_NumRTFull.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_NumRTFull"), statistics::units::Count::get(), "number of row table full events"));
        IND_AvgWordsPerCacheLine.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgWordsPerCacheLine"), statistics::units::Count::get(), "average number of words per cacheline"));
        IND_AvgCacheLinesPerRow.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgCacheLinesPerRow"), statistics::units::Count::get(), "average number of cachelines per row"));
        IND_AvgRowsPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgRowsPerInst"), statistics::units::Count::get(), "average number of rows per indirect instruction"));
        IND_AvgUniqueWordsPerCacheLine.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgUniqueWordsPerCacheLine"), statistics::units::Count::get(), "average number of unique words per cacheline"));
        IND_AvgUniqueCacheLinesPerRow.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgUniqueCacheLinesPerRow"), statistics::units::Count::get(), "average number of unique cachelines per row"));
        IND_AvgUniqueRowsPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgUniqueRowsPerInst"), statistics::units::Count::get(), "average number of unique rows per indirect instruction"));
        IND_AvgRTFullsPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgRTFullsPerInst"), statistics::units::Count::get(), "average number of row table full events per indirect instruction"));
        IND_CyclesFill.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_CyclesFill"), statistics::units::Count::get(), "number of cycles in the FILL stage"));
        IND_CyclesBuild.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_CyclesBuild"), statistics::units::Count::get(), "number of cycles in the BUILD stage"));
        IND_CyclesRequest.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_CyclesRequest"), statistics::units::Count::get(), "number of cycles in the REQUEST stage"));
        IND_CyclesRTAccess.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_CyclesRTAccess"), statistics::units::Count::get(), "number of cycles spent on row table access"));
        IND_CyclesSPDReadAccess.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_CyclesSPDReadAccess"), statistics::units::Count::get(), "number of cycles spent on SPD read access"));
        IND_CyclesSPDWriteAccess.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_CyclesSPDWriteAccess"), statistics::units::Count::get(), "number of cycles spent on SPD write access"));
        IND_AvgCyclesFillPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgCyclesFillPerInst"), statistics::units::Count::get(), "average number of cycles in the FILL stage per indirect instruction"));
        IND_AvgCyclesBuildPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgCyclesBuildPerInst"), statistics::units::Count::get(), "average number of cycles in the BUILD stage per indirect instruction"));
        IND_AvgCyclesRequestPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgCyclesRequestPerInst"), statistics::units::Count::get(), "average number of cycles in the REQUEST stage per indirect instruction"));
        IND_AvgCyclesRTAccessPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgCyclesRTAccessPerInst"), statistics::units::Count::get(), "average number of cycles spent on row table access per indirect instruction"));
        IND_AvgCyclesSPDReadAccessPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgCyclesSPDReadAccessPerInst"), statistics::units::Count::get(), "average number of cycles spent on SPD read access per indirect instruction"));
        IND_AvgCyclesSPDWriteAccessPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgCyclesSPDWriteAccessPerInst"), statistics::units::Count::get(), "average number of cycles spent on SPD write access per indirect instruction"));
        IND_LoadsCacheHitResponding.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_LoadsCacheHitResponding"), statistics::units::Count::get(), "number of loads hit in cache in the M/O state, responding back"));
        IND_LoadsCacheHitAccessing.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_LoadsCacheHitAccessing"), statistics::units::Count::get(), "number of loads hit in cache in the E/S state, reaccessed cache"));
        IND_LoadsMemAccessing.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_LoadsMemAccessing"), statistics::units::Count::get(), "number of loads miss in cache, accessed from memory"));
        IND_LoadsCacheHitRespondingLatency.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_LoadsCacheHitRespondingLatency"), statistics::units::Count::get(), "latency of loads hit in cache in the M/O state, responding back"));
        IND_LoadsCacheHitAccessingLatency.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_LoadsCacheHitAccessingLatency"), statistics::units::Count::get(), "latency of loads hit in cache in the E/S state, reaccessed cache"));
        IND_LoadsMemAccessingLatency.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_LoadsMemAccessingLatency"), statistics::units::Count::get(), "latency of loads miss in cache, accessed from memory"));
        IND_AvgLoadsCacheHitRespondingLatency.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgLoadsCacheHitRespondingLatency"), statistics::units::Count::get(), "average latency of loads hit in cache in the M/O state"));
        IND_AvgLoadsCacheHitAccessingLatency.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgLoadsCacheHitAccessingLatency"), statistics::units::Count::get(), "average latency of loads hit in cache in the E/S state"));
        IND_AvgLoadsMemAccessingLatency.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgLoadsMemAccessingLatency"), statistics::units::Count::get(), "average latency of loads miss in cache"));
        IND_AvgLoadsCacheHitRespondingPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgLoadsCacheHitRespondingPerInst"), statistics::units::Count::get(), "average number of loads hit in cache in the M/O state per indirect instruction"));
        IND_AvgLoadsCacheHitAccessingPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgLoadsCacheHitAccessingPerInst"), statistics::units::Count::get(), "average number of loads hit in cache in the E/S state per indirect instruction"));
        IND_AvgLoadsMemAccessingPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgLoadsMemAccessingPerInst"), statistics::units::Count::get(), "average number of loads miss in cache per indirect instruction"));
        IND_StoresMemAccessing.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_StoresMemAccessing"), statistics::units::Count::get(), "number of writes accessed from memory"));
        IND_AvgStoresMemAccessingPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgStoresMemAccessingPerInst"), statistics::units::Count::get(), "average number of writes accessed from memory per indirect instruction"));
        IND_Evicts.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_Evicts"), statistics::units::Count::get(), "number of evict accesses to the cache side port"));
        IND_AvgEvictssPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgEvictssPerInst"), statistics::units::Count::get(), "average number of evict accesses to the cache side port per indirect instruction"));

        (*IND_NumInsts[indirect_id]).flags(statistics::nozero);
        (*IND_NumWordsInserted[indirect_id]).flags(statistics::nozero);
        (*IND_NumCacheLineInserted[indirect_id]).flags(statistics::nozero);
        (*IND_NumRowsInserted[indirect_id]).flags(statistics::nozero);
        (*IND_NumUniqueWordsInserted[indirect_id]).flags(statistics::nozero);
        (*IND_NumUniqueCacheLineInserted[indirect_id]).flags(statistics::nozero);
        (*IND_NumUniqueRowsInserted[indirect_id]).flags(statistics::nozero);
        (*IND_NumRTFull[indirect_id]).flags(statistics::nozero);
        (*IND_CyclesFill[indirect_id]).flags(statistics::nozero);
        (*IND_CyclesBuild[indirect_id]).flags(statistics::nozero);
        (*IND_CyclesRequest[indirect_id]).flags(statistics::nozero);
        (*IND_CyclesRTAccess[indirect_id]).flags(statistics::nozero);
        (*IND_CyclesSPDReadAccess[indirect_id]).flags(statistics::nozero);
        (*IND_CyclesSPDWriteAccess[indirect_id]).flags(statistics::nozero);
        (*IND_LoadsCacheHitResponding[indirect_id]).flags(statistics::nozero);
        (*IND_LoadsCacheHitAccessing[indirect_id]).flags(statistics::nozero);
        (*IND_LoadsMemAccessing[indirect_id]).flags(statistics::nozero);
        (*IND_LoadsCacheHitRespondingLatency[indirect_id]).flags(statistics::nozero);
        (*IND_LoadsCacheHitAccessingLatency[indirect_id]).flags(statistics::nozero);
        (*IND_LoadsMemAccessingLatency[indirect_id]).flags(statistics::nozero);
        (*IND_StoresMemAccessing[indirect_id]).flags(statistics::nozero);
        (*IND_Evicts[indirect_id]).flags(statistics::nozero);

        (*IND_AvgWordsPerCacheLine[indirect_id]) = (*IND_NumWordsInserted[indirect_id]) / (*IND_NumCacheLineInserted[indirect_id]);
        (*IND_AvgCacheLinesPerRow[indirect_id]) = (*IND_NumCacheLineInserted[indirect_id]) / (*IND_NumRowsInserted[indirect_id]);
        (*IND_AvgRowsPerInst[indirect_id]) = (*IND_NumRowsInserted[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgUniqueWordsPerCacheLine[indirect_id]) = (*IND_NumUniqueWordsInserted[indirect_id]) / (*IND_NumUniqueCacheLineInserted[indirect_id]);
        (*IND_AvgUniqueCacheLinesPerRow[indirect_id]) = (*IND_NumUniqueCacheLineInserted[indirect_id]) / (*IND_NumUniqueRowsInserted[indirect_id]);
        (*IND_AvgUniqueRowsPerInst[indirect_id]) = (*IND_NumUniqueRowsInserted[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgRTFullsPerInst[indirect_id]) = (*IND_NumRTFull[indirect_id]) / (*IND_NumInsts[indirect_id]);

        (*IND_AvgCyclesFillPerInst[indirect_id]) = (*IND_CyclesFill[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgCyclesBuildPerInst[indirect_id]) = (*IND_CyclesBuild[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgCyclesRequestPerInst[indirect_id]) = (*IND_CyclesRequest[indirect_id]) / (*IND_NumInsts[indirect_id]);

        (*IND_AvgCyclesRTAccessPerInst[indirect_id]) = (*IND_CyclesRTAccess[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgCyclesSPDReadAccessPerInst[indirect_id]) = (*IND_CyclesSPDReadAccess[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgCyclesSPDWriteAccessPerInst[indirect_id]) = (*IND_CyclesSPDWriteAccess[indirect_id]) / (*IND_NumInsts[indirect_id]);

        (*IND_AvgLoadsCacheHitRespondingPerInst[indirect_id]) = (*IND_LoadsCacheHitResponding[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgLoadsCacheHitAccessingPerInst[indirect_id]) = (*IND_LoadsCacheHitAccessing[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgLoadsMemAccessingPerInst[indirect_id]) = (*IND_LoadsMemAccessing[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgLoadsCacheHitRespondingLatency[indirect_id]) = (*IND_LoadsCacheHitRespondingLatency[indirect_id]) / (*IND_LoadsCacheHitResponding[indirect_id]);
        (*IND_AvgLoadsCacheHitAccessingLatency[indirect_id]) = (*IND_LoadsCacheHitAccessingLatency[indirect_id]) / (*IND_LoadsCacheHitAccessing[indirect_id]);
        (*IND_AvgLoadsMemAccessingLatency[indirect_id]) = (*IND_LoadsMemAccessingLatency[indirect_id]) / (*IND_LoadsMemAccessing[indirect_id]);
        (*IND_AvgStoresMemAccessingPerInst[indirect_id]) = (*IND_StoresMemAccessing[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgEvictssPerInst[indirect_id]) = (*IND_Evicts[indirect_id]) / (*IND_NumInsts[indirect_id]);

        (*IND_AvgWordsPerCacheLine[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgCacheLinesPerRow[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgRowsPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgUniqueWordsPerCacheLine[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgUniqueCacheLinesPerRow[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgUniqueRowsPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgRTFullsPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgCyclesFillPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgCyclesBuildPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgCyclesRequestPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgCyclesRTAccessPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgCyclesSPDReadAccessPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgCyclesSPDWriteAccessPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgLoadsCacheHitRespondingPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgLoadsCacheHitAccessingPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgLoadsMemAccessingPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgLoadsCacheHitRespondingLatency[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgLoadsCacheHitAccessingLatency[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgLoadsMemAccessingLatency[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgStoresMemAccessingPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgEvictssPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
    }
    for (int stream_id = 0; stream_id < num_maas; stream_id++) {
        STR_NumInsts.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_NumInsts"), statistics::units::Count::get(), "number of instructions"));
        STR_NumWordsInserted.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_NumWordsInserted"), statistics::units::Count::get(), "number of words inserted to the request table"));
        STR_NumCacheLineInserted.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_NumCacheLineInserted"), statistics::units::Count::get(), "number of cachelines inserted to the request table"));
        STR_NumRTFull.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_NumRTFull"), statistics::units::Count::get(), "number of request table full events"));
        STR_AvgWordsPerCacheLine.push_back(new statistics::Formula(this, MAKE_STREAM_STAT_NAME("STR_AvgWordsPerCacheLine"), statistics::units::Count::get(), "average number of words per cacheline"));
        STR_AvgCacheLinesPerInst.push_back(new statistics::Formula(this, MAKE_STREAM_STAT_NAME("STR_AvgCacheLinesPerInst"), statistics::units::Count::get(), "average number of cachelines per stream instruction"));
        STR_AvgRTFullsPerInst.push_back(new statistics::Formula(this, MAKE_STREAM_STAT_NAME("STR_AvgRTFullsPerInst"), statistics::units::Count::get(), "average number of request table full events per stream instruction"));
        STR_CyclesRequest.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_CyclesRequest"), statistics::units::Count::get(), "number of cycles in the REQUEST stage"));
        STR_CyclesRTAccess.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_CyclesRTAccess"), statistics::units::Count::get(), "number of cycles for request table access"));
        STR_CyclesSPDReadAccess.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_CyclesSPDReadAccess"), statistics::units::Count::get(), "number of cycles for SPD read access"));
        STR_CyclesSPDWriteAccess.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_CyclesSPDWriteAccess"), statistics::units::Count::get(), "number of cycles for SPD write access"));
        STR_AvgCyclesRequestPerInst.push_back(new statistics::Formula(this, MAKE_STREAM_STAT_NAME("STR_AvgCyclesRequestPerInst"), statistics::units::Count::get(), "average number of cycles in the REQUEST stage per stream instruction"));
        STR_AvgCyclesRTAccessPerInst.push_back(new statistics::Formula(this, MAKE_STREAM_STAT_NAME("STR_AvgCyclesRTAccessPerInst"), statistics::units::Count::get(), "average number of cycles for request table access per stream instruction"));
        STR_AvgCyclesSPDReadAccessPerInst.push_back(new statistics::Formula(this, MAKE_STREAM_STAT_NAME("STR_AvgCyclesSPDReadAccessPerInst"), statistics::units::Count::get(), "average number of cycles for SPD read access per stream instruction"));
        STR_AvgCyclesSPDWriteAccessPerInst.push_back(new statistics::Formula(this, MAKE_STREAM_STAT_NAME("STR_AvgCyclesSPDWriteAccessPerInst"), statistics::units::Count::get(), "average number of cycles for SPD write access per stream instruction"));
        STR_LoadsCacheAccessing.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_LoadsCacheAccessing"), statistics::units::Count::get(), "number of loads accessed from cache"));
        STR_AvgLoadsCacheAccessingPerInst.push_back(new statistics::Formula(this, MAKE_STREAM_STAT_NAME("STR_AvgLoadsCacheAccessingPerInst"), statistics::units::Count::get(), "average number of loads accessed from cache per stream instruction"));
        STR_Evicts.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_Evicts"), statistics::units::Count::get(), "number of evict accesses to the cache side port"));
        STR_AvgEvictssPerInst.push_back(new statistics::Formula(this, MAKE_STREAM_STAT_NAME("STR_AvgEvictssPerInst"), statistics::units::Count::get(), "average number of evict accesses to the cache side port per stream instruction"));

        (*STR_NumInsts[stream_id]).flags(statistics::nozero);
        (*STR_NumWordsInserted[stream_id]).flags(statistics::nozero);
        (*STR_NumCacheLineInserted[stream_id]).flags(statistics::nozero);
        (*STR_NumRTFull[stream_id]).flags(statistics::nozero);
        (*STR_CyclesRequest[stream_id]).flags(statistics::nozero);
        (*STR_CyclesRTAccess[stream_id]).flags(statistics::nozero);
        (*STR_CyclesSPDReadAccess[stream_id]).flags(statistics::nozero);
        (*STR_CyclesSPDWriteAccess[stream_id]).flags(statistics::nozero);
        (*STR_LoadsCacheAccessing[stream_id]).flags(statistics::nozero);
        (*STR_Evicts[stream_id]).flags(statistics::nozero);

        (*STR_AvgWordsPerCacheLine[stream_id]) = (*STR_NumWordsInserted[stream_id]) / (*STR_NumCacheLineInserted[stream_id]);
        (*STR_AvgCacheLinesPerInst[stream_id]) = (*STR_NumCacheLineInserted[stream_id]) / (*STR_NumInsts[stream_id]);
        (*STR_AvgRTFullsPerInst[stream_id]) = (*STR_NumRTFull[stream_id]) / (*STR_NumInsts[stream_id]);

        (*STR_AvgCyclesRequestPerInst[stream_id]) = (*STR_CyclesRequest[stream_id]) / (*STR_NumInsts[stream_id]);
        (*STR_AvgCyclesRTAccessPerInst[stream_id]) = (*STR_CyclesRTAccess[stream_id]) / (*STR_NumInsts[stream_id]);
        (*STR_AvgCyclesSPDReadAccessPerInst[stream_id]) = (*STR_CyclesSPDReadAccess[stream_id]) / (*STR_NumInsts[stream_id]);
        (*STR_AvgCyclesSPDWriteAccessPerInst[stream_id]) = (*STR_CyclesSPDWriteAccess[stream_id]) / (*STR_NumInsts[stream_id]);

        (*STR_AvgLoadsCacheAccessingPerInst[stream_id]) = (*STR_LoadsCacheAccessing[stream_id]) / (*STR_NumInsts[stream_id]);
        (*STR_AvgEvictssPerInst[stream_id]) = (*STR_Evicts[stream_id]) / (*STR_NumInsts[stream_id]);

        (*STR_AvgWordsPerCacheLine[stream_id]).flags(statistics::nozero | statistics::nonan);
        (*STR_AvgCacheLinesPerInst[stream_id]).flags(statistics::nozero | statistics::nonan);
        (*STR_AvgRTFullsPerInst[stream_id]).flags(statistics::nozero | statistics::nonan);
        (*STR_AvgCyclesRequestPerInst[stream_id]).flags(statistics::nozero | statistics::nonan);
        (*STR_AvgCyclesRTAccessPerInst[stream_id]).flags(statistics::nozero | statistics::nonan);
        (*STR_AvgCyclesSPDReadAccessPerInst[stream_id]).flags(statistics::nozero | statistics::nonan);
        (*STR_AvgCyclesSPDWriteAccessPerInst[stream_id]).flags(statistics::nozero | statistics::nonan);
        (*STR_AvgLoadsCacheAccessingPerInst[stream_id]).flags(statistics::nozero | statistics::nonan);
        (*STR_AvgEvictssPerInst[stream_id]).flags(statistics::nozero | statistics::nonan);
    }
    for (int range_id = 0; range_id < num_maas; range_id++) {
        RNG_NumInsts.push_back(new statistics::Scalar(this, MAKE_RANGE_STAT_NAME("RNG_NumInsts"), statistics::units::Count::get(), "number of instructions"));
        RNG_CyclesCompute.push_back(new statistics::Scalar(this, MAKE_RANGE_STAT_NAME("RNG_CyclesCompute"), statistics::units::Count::get(), "number of compute cycles in range loop"));
        RNG_CyclesSPDReadAccess.push_back(new statistics::Scalar(this, MAKE_RANGE_STAT_NAME("RNG_CyclesSPDReadAccess"), statistics::units::Count::get(), "number of cycles spent on SPD read access in range loop"));
        RNG_CyclesSPDWriteAccess.push_back(new statistics::Scalar(this, MAKE_RANGE_STAT_NAME("RNG_CyclesSPDWriteAccess"), statistics::units::Count::get(), "number of cycles spent on SPD write access in range loop"));
        RNG_AvgCyclesComputePerInst.push_back(new statistics::Formula(this, MAKE_RANGE_STAT_NAME("RNG_AvgCyclesComputePerInst"), statistics::units::Count::get(), "average number of compute cycles per range loop instruction"));
        RNG_AvgCyclesSPDReadAccessPerInst.push_back(new statistics::Formula(this, MAKE_RANGE_STAT_NAME("RNG_AvgCyclesSPDReadAccessPerInst"), statistics::units::Count::get(), "average number of cycles spent on SPD read access per range loop instruction"));
        RNG_AvgCyclesSPDWriteAccessPerInst.push_back(new statistics::Formula(this, MAKE_RANGE_STAT_NAME("RNG_AvgCyclesSPDWriteAccessPerInst"), statistics::units::Count::get(), "average number of cycles spent on SPD write access per range loop instruction"));

        (*RNG_NumInsts[range_id]).flags(statistics::nozero);
        (*RNG_CyclesCompute[range_id]).flags(statistics::nozero);
        (*RNG_CyclesSPDReadAccess[range_id]).flags(statistics::nozero);
        (*RNG_CyclesSPDWriteAccess[range_id]).flags(statistics::nozero);

        (*RNG_AvgCyclesComputePerInst[range_id]) = (*RNG_CyclesCompute[range_id]) / (*RNG_NumInsts[range_id]);
        (*RNG_AvgCyclesSPDReadAccessPerInst[range_id]) = (*RNG_CyclesSPDReadAccess[range_id]) / (*RNG_NumInsts[range_id]);
        (*RNG_AvgCyclesSPDWriteAccessPerInst[range_id]) = (*RNG_CyclesSPDWriteAccess[range_id]) / (*RNG_NumInsts[range_id]);

        (*RNG_AvgCyclesComputePerInst[range_id]).flags(statistics::nozero | statistics::nonan);
        (*RNG_AvgCyclesSPDReadAccessPerInst[range_id]).flags(statistics::nozero | statistics::nonan);
        (*RNG_AvgCyclesSPDWriteAccessPerInst[range_id]).flags(statistics::nozero | statistics::nonan);
    }
    for (int alu_id = 0; alu_id < num_maas; alu_id++) {
        ALU_NumInsts.push_back(new statistics::Scalar(this, MAKE_ALU_STAT_NAME("ALU_NumInsts"), statistics::units::Count::get(), "number of instructions"));
        ALU_NumInstsCompare.push_back(new statistics::Scalar(this, MAKE_ALU_STAT_NAME("ALU_NumInstsCompare"), statistics::units::Count::get(), "number of compare instructions"));
        ALU_NumInstsCompute.push_back(new statistics::Scalar(this, MAKE_ALU_STAT_NAME("ALU_NumInstsCompute"), statistics::units::Count::get(), "number of compute instructions"));
        ALU_CyclesCompute.push_back(new statistics::Scalar(this, MAKE_ALU_STAT_NAME("ALU_CyclesCompute"), statistics::units::Count::get(), "number of cycles spent on compute"));
        ALU_CyclesSPDReadAccess.push_back(new statistics::Scalar(this, MAKE_ALU_STAT_NAME("ALU_CyclesSPDReadAccess"), statistics::units::Count::get(), "number of cycles for SPD read access"));
        ALU_CyclesSPDWriteAccess.push_back(new statistics::Scalar(this, MAKE_ALU_STAT_NAME("ALU_CyclesSPDWriteAccess"), statistics::units::Count::get(), "number of cycles for SPD write access"));
        ALU_AvgCyclesComputePerInst.push_back(new statistics::Formula(this, MAKE_ALU_STAT_NAME("ALU_AvgCyclesComputePerInst"), statistics::units::Count::get(), "average number of cycles spent on compute per ALU instruction"));
        ALU_AvgCyclesSPDReadAccessPerInst.push_back(new statistics::Formula(this, MAKE_ALU_STAT_NAME("ALU_AvgCyclesSPDReadAccessPerInst"), statistics::units::Count::get(), "average number of cycles for SPD read access per ALU instruction"));
        ALU_AvgCyclesSPDWriteAccessPerInst.push_back(new statistics::Formula(this, MAKE_ALU_STAT_NAME("ALU_AvgCyclesSPDWriteAccessPerInst"), statistics::units::Count::get(), "average number of cycles for SPD write access per ALU instruction"));
        ALU_NumComparedWords.push_back(new statistics::Scalar(this, MAKE_ALU_STAT_NAME("ALU_NumComparedWords"), statistics::units::Count::get(), "number of words compared"));
        ALU_NumTakenWords.push_back(new statistics::Scalar(this, MAKE_ALU_STAT_NAME("ALU_NumTakenWords"), statistics::units::Count::get(), "number of words which comparison was taken"));
        ALU_AvgNumTakenWordsPerComparedWords.push_back(new statistics::Formula(this, MAKE_ALU_STAT_NAME("ALU_AvgNumTakenWordsPerComparedWords"), statistics::units::Count::get(), "portion of words which comparison was taken"));

        (*ALU_NumInsts[alu_id]).flags(statistics::nozero);
        (*ALU_NumInstsCompare[alu_id]).flags(statistics::nozero);
        (*ALU_NumInstsCompute[alu_id]).flags(statistics::nozero);
        (*ALU_CyclesCompute[alu_id]).flags(statistics::nozero);
        (*ALU_CyclesSPDReadAccess[alu_id]).flags(statistics::nozero);
        (*ALU_CyclesSPDWriteAccess[alu_id]).flags(statistics::nozero);
        (*ALU_NumComparedWords[alu_id]).flags(statistics::nozero);
        (*ALU_NumTakenWords[alu_id]).flags(statistics::nozero);

        (*ALU_AvgCyclesComputePerInst[alu_id]) = (*ALU_CyclesCompute[alu_id]) / (*ALU_NumInsts[alu_id]);
        (*ALU_AvgCyclesSPDReadAccessPerInst[alu_id]) = (*ALU_CyclesSPDReadAccess[alu_id]) / (*ALU_NumInsts[alu_id]);
        (*ALU_AvgCyclesSPDWriteAccessPerInst[alu_id]) = (*ALU_CyclesSPDWriteAccess[alu_id]) / (*ALU_NumInsts[alu_id]);
        (*ALU_AvgNumTakenWordsPerComparedWords[alu_id]) = (*ALU_NumTakenWords[alu_id]) / (*ALU_NumComparedWords[alu_id]);

        (*ALU_AvgCyclesComputePerInst[alu_id]).flags(statistics::nozero | statistics::nonan);
        (*ALU_AvgCyclesSPDReadAccessPerInst[alu_id]).flags(statistics::nozero | statistics::nonan);
        (*ALU_AvgCyclesSPDWriteAccessPerInst[alu_id]).flags(statistics::nozero | statistics::nonan);
        (*ALU_AvgNumTakenWordsPerComparedWords[alu_id]).flags(statistics::nozero | statistics::nonan);
    }
    INV_NumInvalidatedCachelines = new statistics::Scalar(this, MAKE_INVALIDATOR_STAT_NAME("INV_NumInvalidatedCachelines"), statistics::units::Count::get(), "number of invalidated cachelines");
    INV_AvgInvalidatedCachelinesPerInst = new statistics::Formula(this, MAKE_INVALIDATOR_STAT_NAME("INV_AvgInvalidatedCachelinesPerInst"), statistics::units::Count::get(), "average number of invalidated cachelines per instruction");

    (*INV_NumInvalidatedCachelines).flags(statistics::nozero);
    (*INV_AvgInvalidatedCachelinesPerInst) = (*INV_NumInvalidatedCachelines) / numInst_INV;
    (*INV_AvgInvalidatedCachelinesPerInst).flags(statistics::nozero | statistics::nonan);
}
} // namespace gem5
