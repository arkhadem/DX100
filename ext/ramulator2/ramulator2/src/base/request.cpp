#include "base/request.h"

namespace Ramulator {

Request::Request(Addr_t addr, int type) : addr(addr), type_id(type), region_(-1){};

Request::Request(AddrVec_t addr_vec, int type) : addr_vec(addr_vec), type_id(type), region_(-1){};

Request::Request(Addr_t addr, int type, int source_id, std::function<void(Request &)> callback) : addr(addr), type_id(type), source_id(source_id), callback(callback), region_(-1){};

std::string Request::str() {
    std::stringstream req_stream;
    req_stream << "Request[Type(";
    if (type_id == Type::Read) {
        req_stream << "Read";
    } else {
        req_stream << "Write";
    }
    req_stream << "), ";
    if (addr != -1)
        req_stream << "Address(0x" << std::hex << addr << std::dec << "), ";
    if (addr_vec.size() != 0) {
        req_stream << "Address Vec(";
        if (addr_vec[0] != -1)
            req_stream << "CH: " << addr_vec[0] << ", ";
        if (addr_vec[1] != -1)
            req_stream << "RA: " << addr_vec[1] << ", ";
        if (addr_vec[2] != -1)
            req_stream << "BG: " << addr_vec[2] << ", ";
        if (addr_vec[3] != -1)
            req_stream << "BA: " << addr_vec[3] << ", ";
        if (addr_vec[4] != -1)
            req_stream << "RO: " << addr_vec[4] << ", ";
        if (addr_vec[5] != -1)
            req_stream << "CO: " << addr_vec[5] << "), ";
        req_stream << "), ";
    }
    req_stream << "]";

    return req_stream.str();
}

void Request::setRegion(int8_t region) {
    region_ = region;
}
int8_t Request::getRegion() {
    return region_;
}

} // namespace Ramulator
