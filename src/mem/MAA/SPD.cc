#include "mem/MAA/SPD.hh"
#include "base/trace.hh"
#include "mem/MAA/IF.hh"
#include "mem/MAA/MAA.hh"
#include "mem/MAA/StreamAccess.hh"
#include "mem/MAA/ALU.hh"
#include "mem/MAA/RangeFuser.hh"
#include "mem/MAA/IndirectAccess.hh"
#include "base/types.hh"
#include "debug/SPD.hh"
#include "sim/cur_tick.hh"
#include <cassert>
#include <cstring>

#ifndef TRACING_ON
#define TRACING_ON 1
#endif

namespace gem5 {

///////////////
//
// SPD
//
///////////////
Cycles SPD::getDataLatency(int num_accesses) {
    if (num_accesses == 0) {
        return Cycles(0);
    }
    panic_if(num_accesses < 0, "Invalid number of accesses: %d\n", num_accesses);
    int min_busy_port = 0;
    Tick min_busy_until = read_port_busy_until[0];
    for (int i = 0; i < num_read_ports; i++) {
        if (read_port_busy_until[i] < min_busy_until) {
            min_busy_until = read_port_busy_until[i];
            min_busy_port = i;
        }
    }
    if (read_port_busy_until[min_busy_port] < curTick()) {
        read_port_busy_until[min_busy_port] = curTick();
    }
    read_port_busy_until[min_busy_port] += maa->getCyclesToTicks(Cycles(read_latency * num_accesses));
    DPRINTF(SPD, "%s: read_port_busy_until[%d] = %lu\n", __func__, min_busy_port, read_port_busy_until[min_busy_port]);
    panic_if(read_port_busy_until[min_busy_port] < curTick(),
             "Scheduled read at %lu, but current tick is %lu!\n",
             read_port_busy_until[min_busy_port], curTick());
    return maa->getTicksToCycles(read_port_busy_until[min_busy_port] - curTick());
}
Cycles SPD::setDataLatency(int tile_id, int num_accesses) {
    check_tile_id(tile_id, sizeof(uint32_t));
    if (num_accesses == 0) {
        return Cycles(0);
    }
    panic_if(num_accesses < 0, "Invalid number of accesses: %d\n", num_accesses);
    int min_busy_port = 0;
    Tick min_busy_until = write_port_busy_until[0];
    for (int i = 0; i < num_write_ports; i++) {
        if (write_port_busy_until[i] < min_busy_until) {
            min_busy_until = write_port_busy_until[i];
            min_busy_port = i;
        }
    }
    if (write_port_busy_until[min_busy_port] < curTick()) {
        write_port_busy_until[min_busy_port] = curTick();
    }
    write_port_busy_until[min_busy_port] += maa->getCyclesToTicks(Cycles(write_latency * num_accesses));
    panic_if(write_port_busy_until[min_busy_port] < curTick(),
             "Scheduled write at %lu, but current tick is %lu!\n",
             write_port_busy_until[min_busy_port], curTick());
    DPRINTF(SPD, "%s: write_port_busy_until[%d] = %lu\n", __func__, min_busy_port, write_port_busy_until[min_busy_port]);
    wakeup_waiting_units(tile_id);
    return maa->getTicksToCycles(write_port_busy_until[min_busy_port] - curTick());
}
SPD::TileStatus SPD::getTileStatus(int tile_id) {
    check_tile_id(tile_id, sizeof(uint32_t));
    return tiles_status[tile_id];
}
void SPD::setTileIdle(int tile_id, int word_size) {
    check_tile_id(tile_id, sizeof(uint32_t));
    tiles_status[tile_id] = SPD::TileStatus::Idle;
    if (word_size == 8) {
        tiles_status[tile_id + 1] = SPD::TileStatus::Idle;
    }
    for (int i = 0; i < num_tile_elements * word_size / 4; i++) {
        element_finished[tile_id * num_tile_elements + i] = false;
    }
}
void SPD::setTileFinished(int tile_id, int word_size) {
    check_tile_id(tile_id, sizeof(uint32_t));
    tiles_status[tile_id] = SPD::TileStatus::Finished;
    if (word_size == 8) {
        tiles_status[tile_id + 1] = SPD::TileStatus::Finished;
    }
}
void SPD::setTileService(int tile_id, int word_size) {
    check_tile_id(tile_id, sizeof(uint32_t));
    tiles_status[tile_id] = SPD::TileStatus::Service;
    if (word_size == 8) {
        tiles_status[tile_id + 1] = SPD::TileStatus::Service;
    }
}
void SPD::setTileDirty(int tile_id, int word_size) {
    check_tile_id(tile_id, sizeof(uint32_t));
    tiles_dirty[tile_id] = true;
    if (word_size == 8) {
        tiles_dirty[tile_id + 1] = true;
    }
}
void SPD::setTileClean(int tile_id, int word_size) {
    check_tile_id(tile_id, sizeof(uint32_t));
    tiles_dirty[tile_id] = false;
    if (word_size == 8) {
        tiles_dirty[tile_id + 1] = false;
    }
}
bool SPD::getTileDirty(int tile_id) {
    check_tile_id(tile_id, sizeof(uint32_t));
    return tiles_dirty[tile_id];
}
void SPD::setTileReady(int tile_id, int word_size) {
    check_tile_id(tile_id, sizeof(uint32_t));
    tiles_ready[tile_id]++;
    wakeup_waiting_units(tile_id);
    if (word_size == 8) {
        tiles_ready[tile_id + 1]++;
        wakeup_waiting_units(tile_id + 1);
    }
}
void SPD::setTileNotReady(int tile_id, int word_size) {
    check_tile_id(tile_id, sizeof(uint32_t));
    tiles_ready[tile_id]--;
    if (word_size == 8) {
        tiles_ready[tile_id + 1]--;
    }
}
bool SPD::getTileReady(int tile_id) {
    check_tile_id(tile_id, sizeof(uint32_t));
    return tiles_ready[tile_id] == 0;
}
bool SPD::getElementFinished(int tile_id, int element_id, int word_size, uint8_t func, int id) {
    check_tile_id(tile_id, sizeof(uint32_t));
    bool is_element_finished;
    if (element_id >= num_tile_elements) {
        is_element_finished = false;
    } else {
        int tile_element_id = tile_id * num_tile_elements + element_id * word_size / 4;
        is_element_finished = element_finished[tile_element_id];
    }
    if (is_element_finished == false &&
        (std::find(waiting_units_ids[tile_id].begin(), waiting_units_ids[tile_id].end(), id) == waiting_units_ids[tile_id].end() ||
         std::find(waiting_units_funcs[tile_id].begin(), waiting_units_funcs[tile_id].end(), func) == waiting_units_funcs[tile_id].end())) {
        DPRINTF(SPD, "%s: adding %s[%d] to waiting list tile[%d]\n", __func__, func_unit_names[func], id, tile_id);
        waiting_units_funcs[tile_id].push_back(func);
        waiting_units_ids[tile_id].push_back(id);
    }
    return is_element_finished;
}
void SPD::wakeup_waiting_units(int tile_id) {
    for (int i = 0; i < waiting_units_funcs[tile_id].size(); i++) {
        int waiting_units_id = waiting_units_ids[tile_id][i];
        switch (waiting_units_funcs[tile_id][i]) {
        case (uint8_t)FuncUnitType::ALU: {
            assert(maa->aluUnits[waiting_units_id].getState() == ALUUnit::Status::Work);
            maa->aluUnits[waiting_units_id].scheduleNextExecution(true);
            break;
        }
        case (uint8_t)FuncUnitType::STREAM: {
            assert(maa->streamAccessUnits[waiting_units_id].getState() == StreamAccessUnit::Status::Request);
            maa->streamAccessUnits[waiting_units_id].scheduleNextExecution(true);
            break;
        }
        case (uint8_t)FuncUnitType::INDIRECT: {
            assert(maa->indirectAccessUnits[waiting_units_id].getState() == IndirectAccessUnit::Status::Fill ||
                   maa->indirectAccessUnits[waiting_units_id].getState() == IndirectAccessUnit::Status::Request);
            maa->indirectAccessUnits[waiting_units_id].scheduleNextExecution(true);
            break;
        }
        case (uint8_t)FuncUnitType::RANGE: {
            assert(maa->rangeUnits[waiting_units_id].getState() == RangeFuserUnit::Status::Work);
            maa->rangeUnits[waiting_units_id].scheduleNextExecution(true);
            break;
        }
        }
    }
    waiting_units_funcs[tile_id].clear();
    waiting_units_ids[tile_id].clear();
}
uint16_t SPD::getSize(int tile_id) {
    check_tile_id(tile_id, sizeof(uint32_t));
    panic_if(getTileStatus(tile_id) != SPD::TileStatus::Finished,
             "Trying to get size of an uninitialized tile[%d]!\n",
             tile_id);
    return tiles_size[tile_id];
}
void SPD::setSize(int tile_id, uint16_t size) {
    assert((0 <= tile_id) && (tile_id < num_tiles));
    tiles_size[tile_id] = size;
}
SPD::SPD(MAA *_maa,
         unsigned int _num_tiles,
         unsigned int _num_tile_elements,
         Cycles _read_latency,
         Cycles _write_latency,
         int _num_read_ports,
         int _num_write_ports)
    : num_tiles(_num_tiles),
      num_tile_elements(_num_tile_elements),
      read_latency(_read_latency),
      write_latency(_write_latency),
      num_read_ports(_num_read_ports),
      num_write_ports(_num_write_ports),
      maa(_maa) {

    tiles_data = new uint8_t[num_tiles * num_tile_elements * sizeof(uint32_t)];
    tiles_status = new SPD::TileStatus[num_tiles];
    tiles_dirty = new bool[num_tiles];
    tiles_ready = new uint8_t[num_tiles];
    tiles_size = new uint16_t[num_tiles];
    for (int i = 0; i < num_tiles; i++) {
        tiles_status[i] = SPD::TileStatus::Finished;
        tiles_size[i] = 0;
        tiles_dirty[i] = false;
        tiles_ready[i] = 0;
    }
    element_finished = new bool[num_tiles * num_tile_elements];
    for (int i = 0; i < num_tiles * num_tile_elements; i++) {
        element_finished[i] = true;
    }
    waiting_units_funcs = new std::vector<uint8_t>[num_tiles];
    waiting_units_ids = new std::vector<int>[num_tiles];
    memset(tiles_data, 0, num_tiles * num_tile_elements * sizeof(uint32_t));
    read_port_busy_until = new Tick[num_read_ports];
    write_port_busy_until = new Tick[num_write_ports];
    for (int i = 0; i < num_read_ports; i++) {
        read_port_busy_until[i] = curTick();
    }
    for (int i = 0; i < num_write_ports; i++) {
        write_port_busy_until[i] = curTick();
    }
}
SPD::~SPD() {
    assert(tiles_data != nullptr);
    delete[] tiles_data;
    assert(tiles_status != nullptr);
    delete[] tiles_status;
    assert(tiles_size != nullptr);
    delete[] tiles_size;
    assert(read_port_busy_until != nullptr);
    delete[] read_port_busy_until;
    assert(write_port_busy_until != nullptr);
    delete[] write_port_busy_until;
    assert(element_finished != nullptr);
    delete[] element_finished;
}

///////////////
//
// RF
//
///////////////
RF::RF(unsigned int _num_regs) : num_regs(_num_regs) {
    data = new uint8_t[num_regs * 4];
    memset(data, 0, num_regs * 4 * sizeof(uint8_t));
}
RF::~RF() {
    assert(data != nullptr);
    delete[] data;
}
} // namespace gem5