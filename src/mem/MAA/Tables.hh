#ifndef __MEM_MAA_TABLES_HH__
#define __MEM_MAA_TABLES_HH__

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "base/types.hh"

namespace gem5 {

class MAA;

struct RequestTableEntry {
    RequestTableEntry() : itr(0), wid(0) {}
    RequestTableEntry(int _itr, uint16_t _wid) : itr(_itr), wid(_wid) {}
    uint32_t itr;
    uint16_t wid;
};

class RequestTable {
public:
    RequestTable(MAA *_maa, unsigned int _num_addresses, unsigned int _num_entries_per_address, int _my_unit_id, bool _is_stream = true);
    ~RequestTable();

    bool add_entry(int itr, Addr base_addr, uint16_t wid);
    bool is_full();
    std::vector<RequestTableEntry> get_entries(Addr base_addr);
    void check_reset();
    void reset();

protected:
    unsigned int num_addresses;
    unsigned int num_entries_per_address;
    RequestTableEntry **entries;
    bool **entries_valid;
    Addr *addresses;
    bool *addresses_valid;
    MAA *maa;
    int my_unit_id;
    bool is_stream;
};

struct OffsetTableEntry {
    int itr;
    int wid;
    int next_itr;
};
class OffsetTable {
public:
    OffsetTable() {
        entries = nullptr;
        entries_valid = nullptr;
    }
    ~OffsetTable() {
        if (entries != nullptr) {
            delete[] entries;
            assert(entries_valid != nullptr);
            delete[] entries_valid;
        }
    }
    void allocate(int _my_unit_id,
                  int _num_tile_elements,
                  MAA *_maa,
                  bool _is_stream = false);
    void insert(int itr, int wid, int last_itr);
    std::vector<OffsetTableEntry> get_entry_recv(int first_itr);
    void reset();
    void check_reset();
    OffsetTableEntry *entries;
    bool *entries_valid;
    int num_tile_elements;
    MAA *maa;
    int my_unit_id;
    bool is_stream;
};

class RowTableEntry {
public:
    struct Entry {
        Addr addr;
        int first_itr;
        int last_itr;
    };
    RowTableEntry() {
        entries = nullptr;
        entries_valid = nullptr;
    }
    ~RowTableEntry() {
        if (entries != nullptr) {
            delete[] entries;
            assert(entries_valid != nullptr);
            delete[] entries_valid;
        }
    }
    void allocate(int _my_unit_id,
                  int _my_table_id,
                  int _my_table_row_id,
                  int _num_RT_entries_per_row,
                  OffsetTable *_offset_table,
                  MAA *_maa,
                  bool _is_stream = false);
    bool insert(Addr addr, int itr, int wid);
    bool find_addr(Addr addr);
    void reset();
    void check_reset();
    bool get_entry_send(Addr &addr);
    std::vector<OffsetTableEntry> get_entry_recv(Addr addr);
    bool all_entries_received();
    OffsetTable *offset_table;
    Addr grow_addr;
    Entry *entries;
    bool *entries_valid;
    int num_RT_entries_per_row;
    int last_sent_entry_id;
    MAA *maa;
    int my_unit_id, my_table_id, my_table_row_id;
    bool is_stream;
};
class RowTableSlice {
public:
    RowTableSlice() {
        entries = nullptr;
        entries_valid = nullptr;
        entries_sent = nullptr;
        // entries_full = nullptr;
    }
    ~RowTableSlice() {
        if (entries != nullptr) {
            delete[] entries;
            assert(entries_valid != nullptr);
            delete[] entries_valid;
            assert(entries_sent != nullptr);
            delete[] entries_sent;
            // assert(entries_full != nullptr);
            // delete[] entries_full;
        }
    }
    void allocate(int _my_unit_id,
                  int _my_table_id,
                  int _num_RT_rows_per_slice,
                  int _num_RT_entries_per_row,
                  OffsetTable *_offset_table,
                  MAA *_maa,
                  bool _is_stream = false);
    bool insert(Addr grow_addr, Addr addr, int itr, int wid, bool &first_CL_access);
    bool get_entry_send(Addr &addr, bool drain);
    bool find_next_grow_addr();
    bool is_full();
    void get_send_grow_rowid();
    std::vector<OffsetTableEntry>
    get_entry_recv(Addr grow_addr, Addr addr, bool check_sent);

    void reset();
    void check_reset();
    float getAverageEntriesPerRow();
    OffsetTable *offset_table;
    RowTableEntry *entries;
    bool *entries_valid;
    bool *entries_sent;
    // bool *entries_full;
    int num_RT_rows_per_slice;
    int num_RT_entries_per_row;
    // int last_sent_row_id;
    Addr last_sent_grow_addr;
    int last_sent_rowid;
    int last_sent_grow_rowid;
    MAA *maa;
    int my_unit_id, my_table_id;
    bool is_stream;
};

} // namespace gem5

#endif // __MEM_MAA_TABLES_HH__