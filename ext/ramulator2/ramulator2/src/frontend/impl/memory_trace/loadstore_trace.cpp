#include <filesystem>
#include <fstream>
#include <iostream>

#include "base/exception.h"
#include "base/request.h"
#include "frontend/frontend.h"
#include "translation/translation.h"

// #define SORT_COALESCE_DEBUG

namespace Ramulator {

namespace fs = std::filesystem;

class LoadStoreTrace : public IFrontEnd, public Implementation {
    RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, LoadStoreTrace, "LoadStoreTrace",
                                      "Load/Store memory address trace.")

private:
    // struct Request {
    //     bool is_write;
    //     Addr_t addr;

    //     bool operator==(const Request &other) const { return addr == other.addr && is_write == other.is_write; }
    // };
    std::vector<Request> m_trace;

    size_t m_trace_length = 0;
    size_t m_curr_trace_idx = 0;

    size_t m_trace_count = 0;

    Logger_t m_logger;

    std::string trace_path_str;
    int sort_trace;
    int coalesce_trace;
    int coalesce_sort_trace;
    int interleave_trace;
    bool not_initialized = true;

public:
    int llc_read_access = 0;
    void init() override {
        trace_path_str = param<std::string>("path")
                             .desc("Path to the load store trace file.")
                             .required();
        sort_trace = param<int>("sort_trace").desc("Sort the trace file by address.").default_val(-1);
        coalesce_trace = param<int>("coalesce_trace").desc("Coalesce the trace file by address.").default_val(-1);
        coalesce_sort_trace = param<int>("coalesce_sort_trace").desc("Coalesce and sort the trace file by address.").default_val(-1);
        interleave_trace = param<int>("interleave_trace").desc("Interleave the trace by address.").default_val(-1);

        if (sort_trace != -1 || coalesce_trace != -1) {
            if (coalesce_sort_trace != -1) {
                throw ConfigurationError("coalesce_sort_trace must not be set if sort_trace or coalesce_trace is set.");
            }
        }
        if (interleave_trace != -1) {
            if (coalesce_sort_trace == -1) {
                throw ConfigurationError("coalesce_sort_trace must be set if interleave_trace is set.");
            }
        }

        m_clock_ratio = param<uint>("clock_ratio").required();
        // Create address translation module
        m_translation = create_child_ifce<ITranslation>();

        m_logger = Logging::create_logger("LoadStoreTrace");
        m_logger->info("Loading trace file {} ...", trace_path_str);

        register_stat(llc_read_access).name("llc_read_access");
    };

    void tick() override {
        if (not_initialized) {
            init_trace(trace_path_str, coalesce_trace, sort_trace, coalesce_sort_trace, interleave_trace);
            m_logger->info("Loaded {} lines.", m_trace.size());
            not_initialized = false;
        }
        if (m_trace_count < m_trace_length) {
            const Request &req = m_trace[m_curr_trace_idx];
            bool request_sent = m_memory_system->send(req);
            if (request_sent) {
                m_curr_trace_idx = (m_curr_trace_idx + 1) % m_trace_length;
                m_trace_count++;
            }
        }
    };

private:
    ITranslation *m_translation;

    void init_trace(const std::string &file_path_str, int coalesce_trace, int sort_trace, int coalesce_sort_trace, int interleave_trace) {
        fs::path trace_path(file_path_str);
        if (!fs::exists(trace_path)) {
            throw ConfigurationError("Request {} does not exist!", file_path_str);
        }

        std::ifstream trace_file(trace_path);
        if (!trace_file.is_open()) {
            throw ConfigurationError("Request {} cannot be opened!", file_path_str);
        }

        std::string line;
        while (std::getline(trace_file, line)) {
            std::vector<std::string> tokens;
            tokenize(tokens, line, " ");

            // TODO: Add line number here for better error messages
            if (tokens.size() != 2) {
                throw ConfigurationError("Request {} format invalid!", file_path_str);
            }

            bool is_write = false;
            if (tokens[1] == "R") {
                is_write = false;
            } else if (tokens[1] == "W") {
                continue;
                is_write = true;
            } else {
                throw ConfigurationError("Request {} format invalid!", file_path_str);
            }

            Addr_t addr = -1;
            if (tokens[0].compare(0, 2, "0x") == 0 |
                tokens[0].compare(0, 2, "0X") == 0) {
                addr = std::stoll(tokens[0].substr(2), nullptr, 16) / 64 * 64;
            } else {
                addr = std::stoll(tokens[0]) / 64 * 64;
            }
            Request req = {addr, is_write ? Request::Type::Write : Request::Type::Read};
            req.source_id = 0;
            if (!m_translation->translate(req)) {
                m_logger->error("[CLK {}] translation failed for load request.", m_clk);
                exit(-1);
            };
            m_trace.push_back(req);
        }

        trace_file.close();
        llc_read_access = m_trace.size();

        if (coalesce_sort_trace != -1) {
            std::vector<Request> trace_copy;
            m_logger->info("coalescing and sorting the trace in a group of {} instructions...", coalesce_sort_trace);
            if (interleave_trace != -1) {
                m_logger->info("interleaving the trace...");
            }
            std::vector<Request>::iterator start = m_trace.begin();
            while (start < m_trace.end()) {
                std::vector<Request>::iterator end = start + coalesce_sort_trace;
                if (end > m_trace.end()) {
                    end = m_trace.end();
                }
                std::vector<Request>::iterator current = start;
                std::vector<Request> trace_tmp;
                while (current < end) {
                    if (std::find(trace_tmp.begin(), trace_tmp.end(), *current) == trace_tmp.end()) {
                        trace_tmp.push_back(*current);
                    }
#ifdef SORT_COALESCE_DEBUG
                    else {
                        m_logger->info("removing: {}", current->str());
                    }
#endif
                    current++;
                }

                std::sort(trace_tmp.begin(), trace_tmp.end());

#ifdef SORT_COALESCE_DEBUG
                {
                    current = start;
                    int idx = 0;
                    while (current < end) {
                        m_logger->info("{}: {}", idx, current->str());
                        idx++;
                        current++;
                    }
                    m_logger->info("coalesced and sorted to:");
                    idx = 0;
                    for (auto &t : trace_tmp) {
                        m_logger->info("{}: {}", idx, t.str());
                        idx++;
                    }
                    m_logger->info("\n\n\n");
                }
#endif

                if (interleave_trace != -1) {
                    std::map<int, std::map<long, std::vector<Request>>> reordered_trace;
                    for (auto &req : trace_tmp) {
                        int BG = req.addr_vec[2];
                        long BA_RO = req.addr_vec[3] * (1 << 17) + req.addr_vec[4];
                        reordered_trace[BG][BA_RO].push_back(req);
                    }

                    int idx = 0;
                    for (auto &bg : reordered_trace) {
                        for (auto &ba_ro : bg.second) {
                            for (auto &req : ba_ro.second) {
                                trace_tmp[idx] = req;
                                idx += 2;
                                if (idx >= trace_tmp.size()) {
                                    idx = 1;
                                }
                            }
                        }
                    }
                }

#ifdef SORT_COALESCE_DEBUG
                {
                    int idx = 0;
                    m_logger->info("interleaved to:");
                    for (auto &t : trace_tmp) {
                        m_logger->info("{}: {}", idx, t.str());
                        idx++;
                    }
                    m_logger->info("\n\n\n");
                }
#endif

                copy(trace_tmp.begin(), trace_tmp.end(), back_inserter(trace_copy));
                start = end;
            }
            m_logger->info("{} instructions coalesced and sorted to {} instructions.", m_trace.size(), trace_copy.size());
            m_trace = trace_copy;
        }

        if (coalesce_trace != -1) {
            std::vector<Request> trace_copy;
            m_logger->info("coalescing the trace in a group of {} instructions...", coalesce_trace);
            std::vector<Request>::iterator start = m_trace.begin();
            while (start < m_trace.end()) {
                std::vector<Request>::iterator end = start + coalesce_trace;
                if (end > m_trace.end()) {
                    end = m_trace.end();
                }
                std::vector<Request>::iterator current = start;
                std::vector<Request> trace_tmp;
                while (current < end) {
                    if (std::find(trace_tmp.begin(), trace_tmp.end(), *current) == trace_tmp.end()) {
                        trace_tmp.push_back(*current);
                    }
#ifdef SORT_COALESCE_DEBUG
                    else {
                        m_logger->info("removing: {}", current->addr, current->str());
                    }
#endif
                    current++;
                }

#ifdef SORT_COALESCE_DEBUG
                current = start;
                int idx = 0;
                while (current < end) {
                    m_logger->info("{}: {}", idx, current->str());
                    idx++;
                    current++;
                }
                m_logger->info("coalesced to:");
                idx = 0;
                for (auto &t : trace_tmp) {
                    m_logger->info("{}: {}", idx, t.str());
                    idx++;
                }
                m_logger->info("\n\n\n");
#endif

                copy(trace_tmp.begin(), trace_tmp.end(), back_inserter(trace_copy));
                start = end;
            }
            m_logger->info("{} instructions coalesced to {} instructions.", m_trace.size(), trace_copy.size());
            m_trace = trace_copy;
        }

        if (sort_trace != -1) {
            m_logger->info("sorting the trace in a group of {} instructions...", sort_trace);
            std::vector<Request>::iterator start = m_trace.begin();
            while (start < m_trace.end()) {
                std::vector<Request>::iterator end = start + sort_trace;
                if (end > m_trace.end()) {
                    end = m_trace.end();
                }

#ifdef SORT_COALESCE_DEBUG
                std::vector<Request>::iterator current = start;
                int idx = 0;
                while (current < end) {
                    m_logger->info("{}: {}", idx, current->addr);
                    idx++;
                    current++;
                }
                m_logger->info("sorted to:");
#endif

                if (sort_trace) {
                    std::sort(start, end, [](Request a, Request b) { return a.addr < b.addr; });
                }

#ifdef SORT_COALESCE_DEBUG
                current = start;
                idx = 0;
                while (current < end) {
                    m_logger->info("{}: {}", idx, current->addr);
                    idx++;
                    current++;
                }
#endif

                start = end;
            }
        }

        m_trace_length = m_trace.size();

#ifdef SORT_COALESCE_DEBUG
        exit(0);
#endif
    };

    // TODO: FIXME
    bool is_finished() override { return m_trace_count >= m_trace_length; };

    int get_num_cores() override {
        return 1;
    };
};

} // namespace Ramulator