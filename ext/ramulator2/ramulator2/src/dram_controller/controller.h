#ifndef RAMULATOR_CONTROLLER_CONTROLLER_H
#define RAMULATOR_CONTROLLER_CONTROLLER_H

#include <vector>
#include <deque>

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include "base/base.h"
#include "dram/dram.h"
#include "dram_controller/scheduler.h"
#include "dram_controller/plugin.h"
#include "dram_controller/refresh.h"

namespace Ramulator {

class IDRAMController : public Clocked<IDRAMController> {
    RAMULATOR_REGISTER_INTERFACE(IDRAMController, "Controller", "Memory Controller Interface");

public:
    IDRAM *m_dram = nullptr;
    IScheduler *m_scheduler = nullptr;
    IRefreshManager *m_refresh = nullptr;

    int m_system_id = 0;
    int m_channel_id = -1;
    int m_channel_count = 1;

public:
    /**
     * @brief       Send a request to the memory controller.
     * 
     * @param    req        The request to be enqueued.
     * @return   true       Successful.
     * @return   false      Failed (e.g., buffer full).
     */
    virtual bool send(Request &req) = 0;

    /**
     * @brief       Send a high-priority request to the memory controller.
     * 
     */
    virtual bool priority_send(Request &req) = 0;

    /**
     * @brief       Ticks the memory controller.
     * 
     */
    virtual void tick() = 0;

    virtual bool is_finished() = 0;

    virtual void reset_stats() = 0;
    virtual void dump_stats() = 0;
};

} // namespace Ramulator

#endif // RAMULATOR_CONTROLLER_CONTROLLER_H