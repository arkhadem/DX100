#ifndef __MEM_MAA_INVALIDATOR_HH__
#define __MEM_MAA_INVALIDATOR_HH__

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

#include "base/types.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "sim/system.hh"
#include "arch/generic/mmu.hh"
#include "mem/MAA/IF.hh"
namespace gem5 {
class MAA;

class Invalidator {
protected:
    enum class CLStatus : uint8_t {
        Uncached = 0,
        ReadCached = 1,
        WriteCached = 2,
        MAX
    };
    enum class RGStatus : uint8_t {
        Invalid = 0,
        TransientShared = 1,
        UnusedShared = 2,
        UsingShared = 3,
        UsedShared = 4,
        TransientModified = 5,
        UnusedModified = 6,
        UsingModified = 7,
        UsedModified = 8,
        MAX
    };
    std::string rg_status_names[9] = {
        "Invalid",
        "TransientShared",
        "UnusedShared",
        "UsingShared",
        "UsedShared",
        "TransientModified",
        "UnusedModified",
        "UsingModified",
        "UsedModified"};

public:
    enum class Status : uint8_t {
        Idle = 0,
        Decode = 1,
        Request = 2,
        Response = 3,
        max
    };
    Invalidator();
    ~Invalidator();
    void allocate(int _num_maas,
                  int _num_tiles,
                  int _num_tile_elements,
                  Addr _base_addr,
                  MAA *_maa);
    void read(int tile_id, int element_id);
    void write(int tile_id, int element_id);
    bool recvData(int tile_id, int element_id, uint8_t *dataptr);
    void setInstruction(Instruction *_instruction);
    void scheduleExecuteInstructionEvent(int latency = 0);
    void scheduleTransientInstructionEvent(int latency);
    bool getAddrRegionPermit(Instruction *instruction);
    void finishInstruction(Instruction *instruction);
    Status getState() const { return state; }

protected:
    void executeInstruction();
    void transientInstruction();
    void createMyPacket();
    bool sendOutstandingPacket();
    int get_cl_id(int tile_id, int element_id, int word_size);
    int num_tiles, num_tile_elements, num_maas;
    MAA *maa;
    CLStatus *cl_status;
    RGStatus **rg_status;
    int total_cls;
    Instruction *my_instruction;
    int my_word_size;
    EventFunctionWrapper executeInstructionEvent;
    EventFunctionWrapper transientInstructionEvent;
    Status state;
    int my_invalidating_tile, my_i, my_total_invalidations_sent;
    int my_cl_id;
    bool my_outstanding_pkt;
    int my_received_responses;
    Addr my_last_block_addr = 0;
    Addr my_base_addr;
    Tick my_decode_start_tick;
    const Addr block_size = 64;
    Request::Flags flags = Request::INVALIDATE;
    PacketPtr my_pkt;
    std::vector<Instruction *> transientInstructions;
    std::vector<Tick> transientTicks;
};
} // namespace gem5

#endif //__MEM_MAA_INVALIDATOR_HH__