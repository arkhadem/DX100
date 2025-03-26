#include "base/request.h"
#include "memory_system/memory_system.h"
#include "translation/translation.h"
#include "dram_controller/controller.h"
#include "addr_mapper/addr_mapper.h"
#include "dram/dram.h"
#include <cstdio>

#define MAKE_STAT_NAME(n) \
    (idx == MAX_CMD_REGIONS) ? (std::string("SYS") + std::to_string(m_system_id) + std::string("_") + std::string(n) + std::string("_T")).c_str() : (std::string("SYS") + std::to_string(m_system_id) + std::string("_") + std::string(n) + std::string("_") + std::to_string(idx)).c_str()

namespace Ramulator {

class GenericDRAMSystem final : public IMemorySystem, public Implementation {
    RAMULATOR_REGISTER_IMPLEMENTATION(IMemorySystem, GenericDRAMSystem, "GenericDRAM", "A generic DRAM-based memory system.");

protected:
    Clk_t m_clk = 0;
    Clk_t m_start_clk = 0;
    IDRAM *m_dram;
    IAddrMapper *m_addr_mapper;
    std::vector<IDRAMController *> m_controllers;
    Logger_t m_logger;

public:
    int s_num_read_requests[MAX_CMD_REGIONS + 1];
    int s_num_write_requests[MAX_CMD_REGIONS + 1];
    int s_num_other_requests[MAX_CMD_REGIONS + 1];

public:
    void init() override {
        // Create device (a top-level node wrapping all channel nodes)
        m_dram = create_child_ifce<IDRAM>();
        m_addr_mapper = create_child_ifce<IAddrMapper>();

        m_num_channels = m_dram->get_level_size("channel");

        // Create memory controllers
        for (int i = 0; i < m_num_channels; i++) {
            IDRAMController *controller = create_child_ifce<IDRAMController>();
            m_controllers.push_back(controller);
        }

        m_clock_ratio = param<uint>("clock_ratio").required();
    };

    void setup(IFrontEnd *frontend, IMemorySystem *memory_system) override {
        m_logger = Logging::create_logger("GenericDRAMSystem[" + std::to_string(m_system_id) + "]");
        set_id(fmt::format("System {}", m_system_id));

        m_dram->m_system_id = m_system_id;
        m_dram->m_impl->set_id(fmt::format("DRAM {}", m_system_id));

        m_addr_mapper->m_system_id = m_system_id;
        m_addr_mapper->m_system_count = m_system_count;
        m_addr_mapper->m_impl->set_id(fmt::format("AddrMapper {}", m_system_id));

        int base_channel_id = m_system_id * m_num_channels;
        for (int i = 0; i < m_controllers.size(); i++) {
            m_controllers[i]->m_system_id = m_system_id;
            m_controllers[i]->m_channel_id = base_channel_id;
            m_controllers[i]->m_impl->set_id(fmt::format("Channel {}", base_channel_id));
            base_channel_id++;
        }

        register_stat(m_clk).name(fmt::format("SYS{}_memory_system_total_cycles", m_system_id));
        register_stat(m_start_clk).name(fmt::format("SYS{}_memory_system_ROI_cycles", m_system_id));
        for (int idx = 0; idx < MAX_CMD_REGIONS + 1; idx++) {
            s_num_read_requests[idx] = 0;
            s_num_write_requests[idx] = 0;
            s_num_other_requests[idx] = 0;
            register_stat(s_num_read_requests[idx]).name(MAKE_STAT_NAME("total_num_read_requests"));
            register_stat(s_num_write_requests[idx]).name(MAKE_STAT_NAME("total_num_write_requests"));
            register_stat(s_num_other_requests[idx]).name(MAKE_STAT_NAME("total_num_other_requests"));
        }
    }

    bool send(Request req) override {
        m_addr_mapper->apply(req);
        int channel_id = req.addr_vec[0];
        bool is_success = m_controllers[channel_id]->send(req);

        if (is_success) {
            switch (req.type_id) {
            case Request::Type::Read: {
                int8_t reg = req.getRegion();
                if (reg != -1)
                    s_num_read_requests[reg]++;
                s_num_read_requests[MAX_CMD_REGIONS]++;
                break;
            }
            case Request::Type::Write: {
                int8_t reg = req.getRegion();
                if (reg != -1)
                    s_num_write_requests[reg]++;
                s_num_write_requests[MAX_CMD_REGIONS]++;
                break;
            }
            default: {
                int8_t reg = req.getRegion();
                if (reg != -1)
                    s_num_other_requests[reg]++;
                s_num_other_requests[MAX_CMD_REGIONS]++;
                break;
            }
            }
        }

        return is_success;
    };

    void tick() override {
        // printf("Generic DRAM system tick\n");
        m_clk++;
        m_dram->tick();
        for (auto controller : m_controllers) {
            controller->tick();
        }
    };

    float get_tCK() override {
        return m_dram->m_timing_vals("tCK_ps") / 1000.0f;
    }

    bool is_finished() override {
        for (auto controller : m_controllers) {
            if (controller->is_finished() == false) {
                return false;
            }
        }
        return true;
    };

    void reset_stats() override {
        printf("Resetting generic DRAM system stats\n");
        for (int idx = 0; idx < MAX_CMD_REGIONS + 1; idx++) {
            s_num_read_requests[idx] = 0;
            s_num_write_requests[idx] = 0;
            s_num_other_requests[idx] = 0;
        }
        for (auto controller : m_controllers) {
            controller->reset_stats();
        }
        m_start_clk = m_clk;
    };

    void dump_stats() override {
        m_start_clk = m_clk - m_start_clk;
        YAML::Emitter emitter;
        emitter << YAML::BeginMap;
        m_impl->print_stats(emitter);
        emitter << YAML::EndMap;
        std::cout << emitter.c_str() << std::endl;
        for (auto controller : m_controllers) {
            controller->dump_stats();
        }
    }

    void getAddrMapData(std::vector<int> &m_org,
                        std::vector<int> &m_addr_bits,
                        int &m_num_levels,
                        int &m_tx_offset,
                        int &m_col_bits_idx,
                        int &m_row_bits_idx) override {
        m_org = m_dram->m_organization.count;
        m_org[m_dram->m_levels("channel")] *= m_system_count;
        m_num_levels = m_org.size();
        m_addr_bits.resize(m_num_levels);
        for (size_t level = 0; level < m_addr_bits.size(); level++) {
            m_addr_bits[level] = log2(m_org[level]);
        }
        // Last (Column) address have the granularity of the prefetch size
        m_addr_bits[m_num_levels - 1] -= calc_log2(m_dram->m_internal_prefetch_size);
        int tx_bytes = m_dram->m_internal_prefetch_size * m_dram->m_channel_width / 8;
        m_tx_offset = log2(tx_bytes);
        // Determine where are the row and col bits for ChRaBaRoCo and RoBaRaCoCh
        try {
            m_row_bits_idx = m_dram->m_levels("row");
        } catch (const std::out_of_range &r) {
            throw std::runtime_error(fmt::format("Organization \"row\" not found in the spec, cannot use linear mapping!"));
        }
        // Assume column is always the last level
        m_col_bits_idx = m_num_levels - 1;
        printf("Ramulator organization [n_levels: %d] -- CH: %d, RA: %d, BG: %d, BA: %d, RO: %d, CO: %d\n",
               m_num_levels,
               m_org[0],
               m_org[1],
               m_org[2],
               m_org[3],
               m_org[4],
               m_org[5]);
        printf("Ramulator addr_bit -- RO: %d, BA: %d, BG: %d, RA: %d, CO: %d, CH: %d, TX: %d\n",
               m_addr_bits[4],
               m_addr_bits[3],
               m_addr_bits[2],
               m_addr_bits[1],
               m_addr_bits[5],
               m_addr_bits[0],
               m_tx_offset);
    }

    // const SpecDef& get_supported_requests() override {
    //   return m_dram->m_requests;
    // };
};

} // namespace Ramulator
