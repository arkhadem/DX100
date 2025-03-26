#include "base/request.h"
#include "dram_controller/controller.h"
#include "memory_system/memory_system.h"
#include <cstdio>

#define MAKE_STAT_NAME(n) \
    (idx == MAX_CMD_REGIONS) ? (std::string(n) + std::string("_T")).c_str() : (std::string(n) + std::string("_") + std::to_string(idx)).c_str()

namespace Ramulator {

class GenericDRAMController final : public IDRAMController, public Implementation {
    RAMULATOR_REGISTER_IMPLEMENTATION(IDRAMController, GenericDRAMController, "Generic", "A generic DRAM controller.");

private:
    std::deque<Request> pending; // A queue for read requests that are about to finish (callback after RL)

    ReqBuffer m_active_buffer;   // Buffer for requests being served. This has the highest priority
    ReqBuffer m_priority_buffer; // Buffer for high-priority requests (e.g., maintenance like refresh).
    ReqBuffer m_read_buffer;     // Read request buffer
    ReqBuffer m_write_buffer;    // Write request buffer

    int m_row_addr_idx = -1;

    float m_wr_low_watermark;
    float m_wr_high_watermark;
    int m_queue_size = 32;
    bool m_is_write_mode = false;

    std::vector<IControllerPlugin *> m_plugins;

    std::map<int, int> s_num_commands[MAX_CMD_REGIONS + 1];
    unsigned long long s_queue_total_occupancy[MAX_CMD_REGIONS + 1];
    unsigned long long m_clk_start;
    float s_queue_avg_occupancy[MAX_CMD_REGIONS + 1];
    int s_queue_max_occupancy[MAX_CMD_REGIONS + 1];
    int s_queue_full[MAX_CMD_REGIONS + 1];
    int s_queue_empty[MAX_CMD_REGIONS + 1];

public:
    void init() override {
        m_wr_low_watermark = param<float>("wr_low_watermark").desc("Threshold for switching back to read mode.").default_val(0.2f);
        m_wr_high_watermark = param<float>("wr_high_watermark").desc("Threshold for switching to write mode.").default_val(0.8f);
        m_queue_size = param<int>("queue_size").desc("Size of the read and write buffers.").default_val(32);
        printf("Ramulator2::Queue size: %d\n", m_queue_size);
        m_read_buffer.max_size = m_queue_size;
        m_write_buffer.max_size = m_queue_size;
        m_active_buffer.max_size = m_queue_size;
        m_priority_buffer.max_size = m_queue_size;

        m_scheduler = create_child_ifce<IScheduler>();
        m_refresh = create_child_ifce<IRefreshManager>();

        if (m_config["plugins"]) {
            YAML::Node plugin_configs = m_config["plugins"];
            for (YAML::iterator it = plugin_configs.begin(); it != plugin_configs.end(); ++it) {
                m_plugins.push_back(create_child_ifce<IControllerPlugin>(*it));
            }
        }
    };

    void setup(IFrontEnd *frontend, IMemorySystem *memory_system) override {
        m_dram = memory_system->get_ifce<IDRAM>(fmt::format("DRAM {}", m_system_id));
        m_channel_count = m_dram->m_organization.count[m_dram->m_levels("channel")];
        m_row_addr_idx = m_dram->m_levels("row");
        m_priority_buffer.max_size = 512 * 3 + 32;
        m_logger = Logging::create_logger("GenericDRAMController[" + std::to_string(m_channel_id) + "]");
        for (int idx = 0; idx < MAX_CMD_REGIONS + 1; idx++) {
            for (int command_id = 0; command_id < m_dram->m_commands.size(); command_id++) {
                s_num_commands[idx][command_id] = 0;
                register_stat(s_num_commands[idx][command_id])
                    .name(MAKE_STAT_NAME(fmt::format("CH{}_num_{}_commands", m_channel_id, std::string(m_dram->m_commands(command_id)))))
                    .desc(fmt::format("total number of {} commands", std::string(m_dram->m_commands(command_id))));
            }
            s_queue_max_occupancy[idx] = 0;
            register_stat(s_queue_max_occupancy[idx])
                .name(MAKE_STAT_NAME(fmt::format("CH{}_max_queue_occupancy", m_channel_id)))
                .desc(fmt::format("maximum occupancy of instruction queue"));
            s_queue_avg_occupancy[idx] = 0;
            register_stat(s_queue_avg_occupancy[idx])
                .name(MAKE_STAT_NAME(fmt::format("CH{}_avg_queue_occupancy", m_channel_id)))
                .desc(fmt::format("average occupancy of instruction queue"));
            s_queue_full[idx] = 0;
            register_stat(s_queue_full[idx])
                .name(MAKE_STAT_NAME(fmt::format("CH{}_queue_full", m_channel_id)))
                .desc(fmt::format("number of times the instruction receive rejected because queue was full"));
            s_queue_empty[idx] = 0;
            register_stat(s_queue_empty[idx])
                .name(MAKE_STAT_NAME(fmt::format("CH{}_queue_empty", m_channel_id)))
                .desc(fmt::format("number of times the request queue has been empty"));
            s_queue_total_occupancy[idx] = 0;
        }
        m_clk_start = 0;
    };

    bool send(Request &req) override {
        req.final_command = m_dram->m_request_translations(req.type_id);

        // Forward existing write requests to incoming read requests
        if (req.type_id == Request::Type::Read) {
            auto compare_addr = [req](const Request &wreq) {
                return wreq.addr == req.addr;
            };
            if (std::find_if(m_write_buffer.begin(), m_write_buffer.end(), compare_addr) != m_write_buffer.end()) {
                // The request will depart at the next cycle
                req.depart = m_clk + 1;
                pending.push_back(req);
                return true;
            }
        }

        // Else, enqueue them to corresponding buffer based on request type id
        bool is_success = false;
        req.arrive = m_clk;
        if (req.type_id == Request::Type::Read) {
            is_success = m_read_buffer.enqueue(req);
        } else if (req.type_id == Request::Type::Write) {
            is_success = m_write_buffer.enqueue(req);
        } else {
            throw std::runtime_error("Invalid request type!");
        }
        if (!is_success) {
            // We could not enqueue the request
            req.arrive = -1;
            int8_t reg = req.getRegion();
            if (reg != -1)
                s_queue_full[reg]++;
            s_queue_full[MAX_CMD_REGIONS]++;
            // printf("[%d][%ld] Request %s not received, queue full: R %ld W %ld\n",
            //        m_channel_id, m_clk, req.str().c_str(), m_read_buffer.size(), m_write_buffer.size());
            return false;
        }

        // printf("[%d][%ld] Request %s received successfully\n", m_channel_id, m_clk, req.str().c_str());
        return true;
    };

    bool priority_send(Request &req) override {
        req.final_command = m_dram->m_request_translations(req.type_id);

        bool is_success = false;
        is_success = m_priority_buffer.enqueue(req);
        return is_success;
    }

    void tick() override {
        m_clk++;
        s_queue_max_occupancy[MAX_CMD_REGIONS] = s_queue_max_occupancy[MAX_CMD_REGIONS] < m_read_buffer.size() ? m_read_buffer.size() : s_queue_max_occupancy[MAX_CMD_REGIONS];
        s_queue_total_occupancy[MAX_CMD_REGIONS] += m_read_buffer.size();
        s_queue_avg_occupancy[MAX_CMD_REGIONS] = (double)s_queue_total_occupancy[MAX_CMD_REGIONS] / (double)m_clk;
        if (m_read_buffer.size() == 0) {
            s_queue_empty[MAX_CMD_REGIONS]++;
        }

        // 1. Serve completed reads
        serve_completed_reads();

        m_refresh->tick();

        // 2. Try to find a request to serve.
        ReqBuffer::iterator req_it;
        ReqBuffer *buffer = nullptr;
        bool request_found = schedule_request(req_it, buffer);

        // 3. Update all plugins
        for (auto plugin : m_plugins) {
            plugin->update(request_found, req_it);
        }

        // 4. Finally, issue the commands to serve the request
        if (request_found) {
            // If we find a real request to serve
            // m_logger->debug("[{}] ({}/{}) Issuing {} for {}", m_clk, m_read_buffer.size(), m_active_buffer.size(), std::string(m_dram->m_commands(req_it->command)).c_str(), req_it->str());
            m_dram->issue_command(req_it->command, req_it->addr_vec);
            int8_t reg = req_it->getRegion();
            if (reg != -1)
                s_num_commands[reg][req_it->command]++;
            s_num_commands[MAX_CMD_REGIONS][req_it->command]++;

            // If we are issuing the last command, set depart clock cycle and move the request to the pending queue
            if (req_it->command == req_it->final_command) {
                if (req_it->type_id == Request::Type::Read) {
                    req_it->depart = m_clk + m_dram->m_read_latency;
                    pending.push_back(*req_it);
                } else if (req_it->type_id == Request::Type::Write) {
                    // TODO: Add code to update statistics
                }
                buffer->remove(req_it);
            } else {
                if (m_dram->m_command_meta(req_it->command).is_opening) {
                    m_active_buffer.enqueue(*req_it);
                    buffer->remove(req_it);
                }
            }
        }
    };

    void reset_stats() override {
        printf("Resetting generic DRAM controller stats\n");
        for (int idx = 0; idx < MAX_CMD_REGIONS + 1; idx++) {
            for (int command_id = 0; command_id < m_dram->m_commands.size(); command_id++) {
                s_num_commands[idx][command_id] = 0;
            }
            s_queue_total_occupancy[idx] = 0;
            s_queue_avg_occupancy[idx] = 0;
            s_queue_max_occupancy[idx] = 0;
            s_queue_full[idx] = 0;
            s_queue_empty[idx] = 0;
        }
        m_clk_start = m_clk;
    };

    void dump_stats() override {
        unsigned long long total_clk = m_clk - m_clk_start;
        for (int idx = 0; idx < MAX_CMD_REGIONS + 1; idx++) {
            unsigned long long GIGA = s_queue_total_occupancy[idx] / 1000000000;
            unsigned long long MEGA = (s_queue_total_occupancy[idx] % 1000000000) / 1000000;
            unsigned long long KILO = (s_queue_total_occupancy[idx] % 1000000) / 1000;
            unsigned long long REST = s_queue_total_occupancy[idx] % 1000;
            s_queue_avg_occupancy[idx] = ((double)(GIGA) / (double)total_clk) * (double)1000000000;
            s_queue_avg_occupancy[idx] += ((double)(MEGA) / (double)total_clk) * (double)1000000;
            s_queue_avg_occupancy[idx] += ((double)(KILO) / (double)total_clk) * (double)1000;
            s_queue_avg_occupancy[idx] += ((double)(REST) / (double)total_clk);
        }
        YAML::Emitter emitter;
        emitter << YAML::BeginMap;
        m_impl->print_stats(emitter);
        emitter << YAML::EndMap;
        std::cout << emitter.c_str() << std::endl;
    }

private:
    /**
     * @brief    Helper function to serve the completed read requests
     * @details
     * This function is called at the beginning of the tick() function.
     * It checks the pending queue to see if the top request has received data from DRAM.
     * If so, it finishes this request by calling its callback and poping it from the pending queue.
     */
    void serve_completed_reads() {
        if (pending.size()) {
            // Check the first pending request
            auto &req = pending[0];
            if (req.depart <= m_clk) {
                // Request received data from dram
                if (req.depart - req.arrive > 1) {
                    // Check if this requests accesses the DRAM or is being forwarded.
                    // TODO add the stats back
                }

                if (req.callback) {
                    // If the request comes from outside (e.g., processor), call its callback
                    req.callback(req);
                }
                // Finally, remove this request from the pending queue
                pending.pop_front();
            }
        };
    };

    /**
     * @brief    Checks if we need to switch to write mode
     * 
     */
    void set_write_mode() {
        if (!m_is_write_mode) {
            if ((m_write_buffer.size() > m_wr_high_watermark * m_write_buffer.max_size) || m_read_buffer.size() == 0) {
                m_is_write_mode = true;
            }
        } else {
            if ((m_write_buffer.size() < m_wr_low_watermark * m_write_buffer.max_size) && m_read_buffer.size() != 0) {
                m_is_write_mode = false;
            }
        }
    };

    /**
     * @brief    Helper function to find a request to schedule from the buffers.
     * 
     */
    bool schedule_request(ReqBuffer::iterator &req_it, ReqBuffer *&req_buffer) {
        bool request_found = false;
        // 2.1    First, check the act buffer to serve requests that are already activating (avoid useless ACTs)
        if (req_it = m_scheduler->get_best_request(m_active_buffer); req_it != m_active_buffer.end()) {
            if (m_dram->check_ready(req_it->command, req_it->addr_vec)) {
                request_found = true;
                req_buffer = &m_active_buffer;
            }
        }

        // 2.2    If no requests can be scheduled from the act buffer, check the rest of the buffers
        if (!request_found) {
            // 2.2.1    We first check the priority buffer to prioritize e.g., maintenance requests
            if (m_priority_buffer.size() != 0) {
                req_buffer = &m_priority_buffer;
                req_it = m_priority_buffer.begin();
                // printf("%s\n", req_it->str().c_str());
                req_it->command = m_dram->get_preq_command(req_it->final_command, req_it->addr_vec);

                request_found = m_dram->check_ready(req_it->command, req_it->addr_vec);
                if ((!request_found) & (m_priority_buffer.size() != 0)) {
                    return false;
                }
            }

            // 2.2.1    If no request to be scheduled in the priority buffer, check the read and write buffers.
            if (!request_found) {
                // Query the write policy to decide which buffer to serve
                set_write_mode();
                auto &buffer = m_is_write_mode ? m_write_buffer : m_read_buffer;
                if (req_it = m_scheduler->get_best_request(buffer); req_it != buffer.end()) {
                    request_found = m_dram->check_ready(req_it->command, req_it->addr_vec);
                    req_buffer = &buffer;
                }
            }
        }

        // 2.3 If we find a request to schedule, we need to check if it will close an opened row in the active buffer.
        if (request_found) {
            if (m_dram->m_command_meta(req_it->command).is_closing) {
                bool has_addr_wildcard = false;
                int row_group_end = m_row_addr_idx;
                int last_valid_level = 0;
                for (int i : req_it->addr_vec) {
                    if (i == -1) {
                        has_addr_wildcard = true;
                        row_group_end = last_valid_level;
                        break;
                    }
                    last_valid_level++;
                }

                std::vector<Addr_t> rowgroup((req_it->addr_vec).begin(), (req_it->addr_vec).begin() + row_group_end);
                for (auto _it = m_active_buffer.begin(); _it != m_active_buffer.end(); _it++) {
                    std::vector<Addr_t> _it_rowgroup(_it->addr_vec.begin(), _it->addr_vec.begin() + row_group_end);
                    if (rowgroup == _it_rowgroup) {
                        // Invalidate this scheduling outcome if we are to interrupt a request in the active buffer
                        request_found = false;
                    }
                }
            }
        }

        return request_found;
    }

    bool is_finished() override {
        unsigned long long total_clk = m_clk - m_clk_start;
        for (int idx = 0; idx < MAX_CMD_REGIONS + 1; idx++) {
            unsigned long long GIGA = s_queue_total_occupancy[idx] / 1000000000;
            unsigned long long MEGA = (s_queue_total_occupancy[idx] % 1000000000) / 1000000;
            unsigned long long KILO = (s_queue_total_occupancy[idx] % 1000000) / 1000;
            unsigned long long REST = s_queue_total_occupancy[idx] % 1000;
            s_queue_avg_occupancy[idx] = ((double)(GIGA) / (double)total_clk) * (double)1000000000;
            s_queue_avg_occupancy[idx] += ((double)(MEGA) / (double)total_clk) * (double)1000000;
            s_queue_avg_occupancy[idx] += ((double)(KILO) / (double)total_clk) * (double)1000;
            s_queue_avg_occupancy[idx] += ((double)(REST) / (double)total_clk);
        }
        return m_read_buffer.size() == 0 && m_write_buffer.size() == 0 && m_active_buffer.size() == 0 && m_priority_buffer.size() == 0;
    };
};

} // namespace Ramulator