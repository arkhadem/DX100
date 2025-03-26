#include <filesystem>
#include <iostream>
#include <fstream>

#include "base/request.h"
#include "frontend/frontend.h"
#include "base/exception.h"

namespace Ramulator {

class GEM5 : public IFrontEnd, public Implementation {
    RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, GEM5, "GEM5", "GEM5 frontend.")

public:
    void init() override {};
    void tick() override {};

    bool receive_external_requests(int req_type_id, Addr_t addr, int8_t region, int source_id, std::function<void(Request &)> callback) override {
        Request req(addr, req_type_id, source_id, callback);
        req.setRegion(region);
        return m_memory_system->send(req);
    }

private:
    bool is_finished() override { return true; };
};

} // namespace Ramulator