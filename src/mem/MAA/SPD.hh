#ifndef __MEM_MAA_SPD_HH__
#define __MEM_MAA_SPD_HH__

#include <cassert>
#include <cstdint>
#include <cstring>
#include "base/logging.hh"
#include "base/trace.hh"
#include "base/types.hh"
#include "debug/SPD.hh"

namespace gem5 {
class MAA;

class SPD {
public:
    enum class TileStatus : uint8_t {
        Idle = 0,
        Service = 1,
        Finished = 2,
        MAX
    };
    std::string tile_status_names[4] = {
        "Idle",
        "Service",
        "Finished",
        "MAX"};

protected:
    uint8_t *tiles_data;
    TileStatus *tiles_status;
    bool *tiles_dirty;
    uint8_t *tiles_ready;
    uint16_t *tiles_size;
    bool *element_finished;
    std::vector<uint8_t> *waiting_units_funcs;
    std::vector<int> *waiting_units_ids;
    unsigned int num_tiles;
    unsigned int num_tile_elements;
    Tick *read_port_busy_until;
    Tick *write_port_busy_until;
    const Cycles read_latency, write_latency;
    const int num_read_ports, num_write_ports;
    MAA *maa;

public:
    void check_tile_id(int tile_id, int word_size) {
        panic_if(tile_id < 0 || tile_id >= num_tiles, "Invalid tile_id: %d\n", tile_id);
        panic_if(word_size != 4 && word_size != 8, "Invalid data type size: %d\n", word_size);
        panic_if(word_size == 8 && tile_id >= num_tiles - 1, "Invalid tile_id for 8-byte data type: %d\n", tile_id);
    }
    void check_tile_element_id(int tile_id, int element_id, int word_size) {
        check_tile_id(tile_id, word_size);
        panic_if(element_id < 0 || element_id >= num_tile_elements, "Invalid element_id: %d\n", element_id);
    }
    template <typename T>
    T getData(int tile_id, int element_id) {
        check_tile_element_id(tile_id, element_id, sizeof(T));
        return *((T *)(tiles_data + tile_id * num_tile_elements * 4 + element_id * sizeof(T)));
    }
    uint8_t *getDataPtr(int tile_id, int element_id) {
        check_tile_element_id(tile_id, element_id, sizeof(uint32_t));
        return (uint8_t *)(tiles_data + tile_id * num_tile_elements * 4 + element_id * 4);
    }
    template <typename T>
    void setData(int tile_id, int element_id, T _data) {
        check_tile_element_id(tile_id, element_id, sizeof(T));
        *((T *)(tiles_data + tile_id * num_tile_elements * 4 + element_id * sizeof(T))) = _data;
        int tile_element_id = tile_id * num_tile_elements + element_id * sizeof(T) / 4;
        element_finished[tile_element_id] = true;
        DPRINTF(SPD, "%s: tile[%d] element[%d] tile_element[%d] finished\n", __func__, tile_id, element_id, tile_element_id);
    }
    void setFakeData(int tile_id, int element_id, int word_size) {
        check_tile_element_id(tile_id, element_id, word_size);
        int tile_element_id = tile_id * num_tile_elements + element_id * word_size / 4;
        element_finished[tile_element_id] = true;
        DPRINTF(SPD, "%s: tile[%d] element[%d] tile_element[%d] fake finished\n", __func__, tile_id, element_id, tile_element_id);
    }
    void wakeup_waiting_units(int tile_id);
    Cycles getDataLatency(int num_accesses);
    Cycles setDataLatency(int tile_id, int num_accesses);
    TileStatus getTileStatus(int tile_id);
    bool getElementFinished(int tile_id, int element_id, int word_size, uint8_t func, int id);
    void setTileIdle(int tile_id, int word_size);
    void setTileService(int tile_id, int word_size);
    void setTileFinished(int tile_id, int word_size);
    void setTileDirty(int tile_id, int word_size);
    void setTileClean(int tile_id, int word_size);
    bool getTileDirty(int tile_id);
    void setTileReady(int tile_id, int word_size);
    void setTileNotReady(int tile_id, int word_size);
    bool getTileReady(int tile_id);
    uint16_t getSize(int tile_id);
    void setSize(int tile_id, uint16_t size);

public:
    SPD(MAA *_maa,
        unsigned int _num_tiles,
        unsigned int _num_tile_elements,
        Cycles _read_latency,
        Cycles _write_latency,
        int _num_read_ports,
        int _num_write_ports);

    ~SPD();
};

struct Register {
public:
    int register_id;
    int size;
    int maa_id;
    int core_id;
    uint32_t data_UINT32;
    uint64_t data_UINT64;
};

class RF {
protected:
    uint8_t *data;
    unsigned int num_regs;

public:
    template <typename T>
    void check_reg_id(int reg_id) {
        panic_if(reg_id < 0 || reg_id >= num_regs, "Invalid reg_id: %d\n", reg_id);
        panic_if(sizeof(T) != 4 && sizeof(T) != 8, "Invalid data type size: %d\n", sizeof(T));
        panic_if(sizeof(T) == 8 && reg_id >= num_regs - 1, "Invalid reg_id for 8-byte data type: %d\n", reg_id);
    }
    template <typename T>
    T getData(int reg_id) {
        check_reg_id<T>(reg_id);
        return *((T *)(data + reg_id * 4));
    }
    uint8_t *getDataPtr(int reg_id) {
        check_reg_id<uint32_t>(reg_id);
        return (uint8_t *)(data + reg_id * 4);
    }
    template <typename T>
    void setData(int reg_id, T _data) {
        check_reg_id<T>(reg_id);
        *((T *)(data + reg_id * 4)) = _data;
    }

public:
    RF(unsigned int _num_regs);
    ~RF();
};
} // namespace gem5
#endif // __MEM_MAA_SPD_HH__