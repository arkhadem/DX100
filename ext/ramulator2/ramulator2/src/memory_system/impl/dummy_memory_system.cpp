#include "memory_system/memory_system.h"

namespace Ramulator {

class DummyMemorySystem final : public IMemorySystem, public Implementation {
    RAMULATOR_REGISTER_IMPLEMENTATION(IMemorySystem, DummyMemorySystem, "DummyMemorySystem", "A dummy memory system with zero latency to test the frontend.");

public:
    void init() override {
        m_clock_ratio = param<uint>("clock_ratio").default_val(1);
    };

    bool send(Request req) override {
        if (req.callback) {
            req.callback(req);
        }
        return true;
    };

    void tick() override {};

    bool is_finished() override {
        return true;
    };

    void reset_stats() override {
        return;
    };

    void dump_stats() override {
        return;
    };

    void getAddrMapData(std::vector<int> &m_org,
                        std::vector<int> &m_addr_bits,
                        int &m_num_levels,
                        int &m_tx_offset,
                        int &m_col_bits_idx,
                        int &m_row_bits_idx) override {
        return;
    }
};

} // namespace Ramulator