#ifndef __MEM_MAA_INDIRECT_ACCESS_HH__
#define __MEM_MAA_INDIRECT_ACCESS_HH__

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <map>
#include <set>

#include "base/statistics.hh"
#include "base/types.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "sim/system.hh"
#include "arch/generic/mmu.hh"
#include "mem/MAA/Tables.hh"

namespace gem5 {

class MAA;
class IndirectAccessUnit;
class Instruction;

class IndirectAccessUnit : public BaseMMU::Translation {
public:
    enum class Status : uint8_t {
        Idle = 0,
        Decode = 1,
        Fill = 2,
        Build = 3,
        Request = 4,
        Response = 5,
        max
    };

protected:
    std::string status_names[7] = {
        "Idle",
        "Decode",
        "Fill",
        "Build",
        "Request",
        "Response",
        "max"};
    int total_num_RT_subslices;
    int num_RT_configs;
    int my_RT_config;
    int initial_RT_config;
    int **RT_slice_org;
    int *num_RT_slices;
    int *num_RT_rows_total;
    Addr *num_RT_possible_grows;
    int *num_RT_subslices;
    int *num_RT_slice_columns;
    Addr *RT_config_addr;
    int *RT_config_cache;
    Tick *RT_config_cache_tick;
    int num_tile_elements;
    int num_RT_rows_per_slice;
    int num_RT_entries_per_subslice_row;
    int num_RT_config_cache_entries;
    int num_channels;
    int num_cores;
    bool reconfigure_RT;
    bool reorder_RT;
    int num_initial_RT_slices;
    Status state;
    RowTableSlice **RT;
    OffsetTable *offset_table;
    int dst_tile_id;
    Cycles rowtable_latency;
    std::map<Addr, Tick> LoadsCacheHitRespondingTimeHistory;
    std::map<Addr, Tick> LoadsCacheHitAccessingTimeHistory;
    std::map<Addr, Tick> LoadsMemAccessingTimeHistory;

public:
    MAA *maa;
    IndirectAccessUnit();
    ~IndirectAccessUnit();
    void allocate(int _my_indirect_id,
                  int _num_tile_elements,
                  int _num_row_table_rows_per_slice,
                  int _num_row_table_entries_per_subslice_row,
                  int _num_row_table_config_cache_entries,
                  bool _reconfigure_row_table,
                  bool _reorder_row_table,
                  int _num_initial_row_table_slice,
                  Cycles _rowtable_latency,
                  int _num_channels,
                  int _num_cores,
                  MAA *_maa);
    Status getState() const { return state; }
    bool scheduleNextExecution(bool force = false);
    void scheduleExecuteInstructionEvent(int latency = 0);
    void setInstruction(Instruction *_instruction);
    void memWritePacketSent(Addr addr);
    void memReadPacketSent(Addr addr);
    void cacheWritePacketSent(Addr addr);
    void cacheReadPacketSent(Addr addr);

    bool recvData(const Addr addr, uint8_t *dataptr, bool is_block_cached);

    /* Related to BaseMMU::Translation Inheretance */
    void markDelayed() override {}
    void finish(const Fault &fault, const RequestPtr &req, ThreadContext *tc, BaseMMU::Mode mode) override;

protected:
    Instruction *my_instruction;
    bool my_is_load;
    Request::Flags flags = 0;
    const Addr block_size = 64;
    int my_word_size = -1;
    int my_words_per_cl = -1;
    Addr my_virtual_addr = 0;
    Addr my_base_addr, my_min_addr, my_max_addr;
    int8_t my_addr_range_id;
    int my_dst_tile, my_src_tile, my_src_reg, my_cond_tile, my_max, my_idx_tile;
    bool my_cond_tile_ready, my_idx_tile_ready, my_src_tile_ready;
    int my_expected_responses;
    int my_received_responses;
    std::vector<int> my_sorted_indices;
    bool **my_RT_req_sent;
    std::vector<int> *my_RT_slice_order;
    int my_i, my_RT_idx;
    bool my_fill_finished;
    bool my_force_cache_determined;
    bool my_force_cache;

    bool my_translation_done;
    Addr my_translated_addr;
    int my_indirect_id;
    Tick my_SPD_read_finish_tick;
    Tick my_SPD_write_finish_tick;
    Tick my_RT_read_access_finish_tick;
    Tick my_RT_write_access_finish_tick;
    Tick my_decode_start_tick;
    Tick my_fill_start_tick;
    Tick my_build_start_tick;
    Tick my_request_start_tick;
    std::set<Addr> my_unique_WORD_addrs;
    std::set<Addr> my_unique_CL_addrs;
    std::set<Addr> my_unique_ROW_addrs;

    Addr translatePacket(Addr vaddr);
    bool checkAndResetAllRowTablesSent();
    int getRowTableIdx(int RT_config, int channel, int rank, int bankgroup, int bank);
    Addr getGrowAddr(int RT_config, int bankgroup, int bank, int row);
    int getRowTableConfig(Addr addr);
    void setRowTableConfig(Addr addr, int num_CLs, int num_ROWs);
    void checkTileReady();
    bool checkElementReady();
    bool checkReadyForFinish();
    void fillRowTable(bool &finished, bool &waitForFinish, bool &waitForElement, bool &needDrain, int &num_spd_read_condidx_accesses, int &num_rowtable_accesses);
    void executeInstruction();
    EventFunctionWrapper executeInstructionEvent;
    void check_reset();
    Cycles updateLatency(int num_spd_read_data_accesses,
                         int num_spd_read_condidx_accesses,
                         int num_spd_write_accesses,
                         int num_rowtable_read_accesses,
                         int num_rowtable_write_accesses,
                         int RT_access_parallelism);

public:
    void createReadPacket(Addr addr, int latency);
};
} // namespace gem5

#endif //__MEM_MAA_INDIRECT_ACCESS_HH__
