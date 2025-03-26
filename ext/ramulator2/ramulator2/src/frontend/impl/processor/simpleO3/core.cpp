#include <cassert>
#include <filesystem>
#include <iostream>
#include <fstream>

#include <spdlog/spdlog.h>
#include <string>

#include "base/exception.h"
#include "base/utils.h"
#include "frontend/impl/processor/simpleO3/core.h"
#include "frontend/impl/processor/simpleO3/llc.h"

namespace Ramulator {

namespace fs = std::filesystem;

SimpleO3Core::Trace::Trace(std::string file_path_str, int bubble_count) {
    fs::path trace_path(file_path_str);
    if (!fs::exists(trace_path)) {
        throw ConfigurationError("Trace {} does not exist!", file_path_str);
    }

    std::ifstream trace_file(trace_path);
    if (!trace_file.is_open()) {
        throw ConfigurationError("Trace {} cannot be opened!", file_path_str);
    }

    std::string line;
    while (std::getline(trace_file, line)) {
        std::vector<std::string> tokens;
        tokenize(tokens, line, " ");

        if (tokens.size() != 2) {
            throw ConfigurationError("Trace {} format invalid!", file_path_str);
        }

        bool is_write = false;
        if (tokens[1] == "R") {
            is_write = false;
        } else if (tokens[1] == "W") {
            continue;
            is_write = true;
        } else {
            throw ConfigurationError("Trace {} format invalid!", file_path_str);
        }

        Addr_t load_addr = -1;
        if (tokens[0].compare(0, 2, "0x") == 0 |
            tokens[0].compare(0, 2, "0X") == 0) {
            load_addr = std::stoll(tokens[0].substr(2), nullptr, 16) / 64 * 64;
        } else {
            load_addr = std::stoll(tokens[0]) / 64 * 64;
        }

        m_trace.push_back({bubble_count, load_addr, -1});
    }

    trace_file.close();
    m_trace.push_back({-1, -1, -1});
    m_trace_length = m_trace.size();
}

const SimpleO3Core::Trace::Inst &SimpleO3Core::Trace::get_next_inst() {
    if (m_curr_trace_idx != m_trace_length) {
        const Inst &inst = m_trace[m_curr_trace_idx];
        m_curr_trace_idx += 1;
        return inst;
    }
    assert(false);
    return m_trace[0]; // Dummy return
}

SimpleO3Core::InstWindow::InstWindow(int ipc, int depth) : m_ipc(ipc), m_depth(depth),
                                                           m_ready_list(depth, false), m_addr_list(depth, -1){};

bool SimpleO3Core::InstWindow::is_full() {
    return m_load == m_depth;
}

bool SimpleO3Core::InstWindow::is_empty() {
    return m_load == 0;
}

void SimpleO3Core::InstWindow::insert(bool ready, Addr_t addr) {
    m_ready_list.at(m_head_idx) = ready;
    m_addr_list.at(m_head_idx) = addr;

    m_head_idx = (m_head_idx + 1) % m_depth;
    m_load++;
}

int SimpleO3Core::InstWindow::retire() {
    if (is_empty())
        return 0;

    int num_retired = 0;
    while (m_load > 0 && num_retired < m_ipc) {
        if (!m_ready_list.at(m_tail_idx))
            break;

        m_tail_idx = (m_tail_idx + 1) % m_depth;
        m_load--;
        num_retired++;
    }
    return num_retired;
}

void SimpleO3Core::InstWindow::set_ready(Addr_t addr) {
    if (m_load == 0)
        return;

    int index = m_tail_idx;
    for (int i = 0; i < m_load; i++) {
        if (m_addr_list[index] == addr) {
            m_ready_list[index] = true;
        }
        index++;
        if (index == m_depth) {
            index = 0;
        }
    }
}

SimpleO3Core::SimpleO3Core(int id, int ipc, int depth, std::string trace_path, ITranslation *translation, SimpleO3LLC *llc, int bubble_count) : m_id(id), m_window(ipc, depth), m_trace(trace_path, bubble_count), m_translation(translation), m_llc(llc) {
    // Fetch the instructions and addresses for tick 0
    auto inst = m_trace.get_next_inst();
    m_num_bubbles = inst.bubble_count;
    assert(m_num_bubbles >= 0); // The number of bubbles should be non-negative (0 is allowed
    m_load_addr = inst.load_addr;
    m_writeback_addr = inst.store_addr;
    m_logger = Logging::create_logger("core[" + std::to_string(id) + "]");
}

void SimpleO3Core::tick() {
    m_clk++;

    s_insts_retired += m_window.retire();

    if (reached_expected_num_insts == true) {
        s_cycles_recorded = m_clk;
        return;
    }

    int num_inserted_insts = 0;
    while (num_inserted_insts < m_window.m_ipc) {
        if (m_window.is_full()) {
            m_logger->info("[CLK {}] window is full.", m_clk);
            break;
        }
        if (m_num_bubbles > 0) {
            // First, issue the non-memory instructions
            m_window.insert(true, -1);
            num_inserted_insts++;
            m_num_bubbles--;
        } else if (m_load_addr != -1) {
            // Second, try to send the load to the LLC
            Request load_request(m_load_addr, Request::Type::Read, m_id, m_callback);
            if (!m_translation->translate(load_request)) {
                m_logger->info("[CLK {}] translation failed for load request.", m_clk);
                break;
            };

            if (m_llc->send(load_request)) {
                m_window.insert(false, load_request.addr);
                m_load_addr = -1;
                num_inserted_insts++;
            } else {
                m_logger->info("[CLK {}] LLC cannot accept the load request.", m_clk);
                break;
            }
        } else {
            auto inst = m_trace.get_next_inst();
            m_num_bubbles = inst.bubble_count;
            m_load_addr = inst.load_addr;
            if (m_num_bubbles == -1) {
                // The end of the trace
                reached_expected_num_insts = true;
                m_logger->info("[CLK {}] reached the end of the trace.", m_clk, m_id);
                break;
            }
        }
    }
    m_logger->info("[CLK {}] {} instructions inserted, window size: {}.", m_clk, num_inserted_insts, m_window.m_load);
}

void SimpleO3Core::receive(Request &req) {
    m_window.set_ready(req.addr);

    if (req.arrive != -1 && req.depart > m_last_mem_cycle) {
        s_mem_access_cycles += (req.depart - std::max(m_last_mem_cycle, req.arrive));
        m_last_mem_cycle = req.depart;
    }
}

bool SimpleO3Core::is_finished() {
    if (reached_expected_num_insts) {
        if (m_window.is_empty()) {
            m_logger->info("[CLK {}] finished. {} instructions retired.", m_clk, s_insts_retired);
            return true;
        } else {
            m_logger->info("[CLK {}] not finished. {} instructions in the window.", m_clk, m_window.m_load);
        }
    }
    return false;
}

} // namespace Ramulator
