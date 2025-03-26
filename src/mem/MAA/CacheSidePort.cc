#include "mem/MAA/ALU.hh"
#include "mem/MAA/IF.hh"
#include "mem/MAA/IndirectAccess.hh"
#include "mem/MAA/Invalidator.hh"
#include "mem/MAA/RangeFuser.hh"
#include "mem/MAA/SPD.hh"
#include "mem/MAA/StreamAccess.hh"
#include "mem/MAA/MAA.hh"

#include "base/addr_range.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "mem/packet.hh"
#include "params/MAA.hh"
#include "debug/MAA.hh"
#include "debug/MAACpuPort.hh"
#include "debug/MAACachePort.hh"
#include "debug/MAAMemPort.hh"
#include "debug/MAAController.hh"
#include "sim/cur_tick.hh"
#include <cassert>
#include <cstdint>

#ifndef TRACING_ON
#define TRACING_ON 1
#endif

namespace gem5 {

bool MAA::CacheSidePort::recvTimingResp(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACachePort, "%s: received %s\n", __func__, pkt->print());
    maa->recvTimingResp(pkt, true);
    outstandingCacheSidePackets--;
    if (blockReason == BlockReason::MAX_XBAR_PACKETS) {
        setUnblocked(BlockReason::MAX_XBAR_PACKETS);
    }
    pkt->deleteData();
    delete pkt;
    return true;
}

void MAA::recvCacheTimingSnoopReq(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACachePort, "%s: received %s\n", __func__, pkt->print());
    assert(false);
}
// Express snooping requests to memside port
void MAA::CacheSidePort::recvTimingSnoopReq(PacketPtr pkt) {
    DPRINTF(MAACachePort, "%s: received %s\n", __func__, pkt->print());
    // handle snooping requests
    maa->recvCacheTimingSnoopReq(pkt);
    assert(false);
}

Tick MAA::recvCacheAtomicSnoop(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACachePort, "%s: received %s\n", __func__, pkt->print());
    assert(false);
    return 0;
}
Tick MAA::CacheSidePort::recvAtomicSnoop(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACachePort, "%s: received %s\n", __func__, pkt->print());
    return maa->recvCacheAtomicSnoop(pkt);
    assert(false);
}

void MAA::cacheFunctionalAccess(PacketPtr pkt, bool from_cpu_side) {
    /// print the packet
    DPRINTF(MAACachePort, "%s: received %s\n", __func__, pkt->print());
    assert(false);
}
void MAA::CacheSidePort::recvFunctionalSnoop(PacketPtr pkt) {
    /// print the packet
    // DPRINTF(MAACachePort, "%s: received %s, doing nothing\n", __func__, pkt->print());
    // // functional snoop (note that in contrast to atomic we don't have
    // // a specific functionalSnoop method, as they have the same
    // // behaviour regardless)
    // maa->cacheFunctionalAccess(pkt, false);
    // assert(false);
}

void MAA::CacheSidePort::recvReqRetry() {
    /// print the packet
    DPRINTF(MAACachePort, "%s: called!\n", __func__);
    setUnblocked(BlockReason::CACHE_FAILED);
}

bool MAA::CacheSidePort::sendPacket(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACachePort, "%s: sending %s to cache\n", __func__, pkt->print());
    if (blockReason != BlockReason::NOT_BLOCKED) {
        DPRINTF(MAACachePort, "%s Send blocked because of %s...\n", __func__, blockReason == BlockReason::MAX_XBAR_PACKETS ? "MAX_XBAR_PACKETS" : "CACHE_FAILED");
        return false;
    }
    if (outstandingCacheSidePackets == maxOutstandingCacheSidePackets) {
        // XBAR is full
        DPRINTF(MAACachePort, "%s Send failed because XBAR is full...\n", __func__);
        assert(blockReason == BlockReason::NOT_BLOCKED);
        blockReason = BlockReason::MAX_XBAR_PACKETS;
        return false;
    }
    if (sendTimingReq(pkt) == false) {
        // Cache cannot receive a new request
        DPRINTF(MAACachePort, "%s Send failed because cache returned false...\n", __func__);
        blockReason = BlockReason::CACHE_FAILED;
        return false;
    }
    DPRINTF(MAACachePort, "%s Send is successfull...\n", __func__);
    if (pkt->needsResponse() && !pkt->cacheResponding())
        outstandingCacheSidePackets++;
    return true;
}
bool MAA::sendPacketCache(PacketPtr pkt) {
    int pkt_bus_id = core_addr(pkt->getAddr());
    return cacheSidePorts[pkt_bus_id]->sendPacket(pkt);
}
void MAA::CacheSidePort::setUnblocked(BlockReason reason) {
    assert(blockReason == reason);
    blockReason = BlockReason::NOT_BLOCKED;
    maa->unblockCache(core_id);
}

void MAA::CacheSidePort::allocate(int _core_id, int _maxOutstandingCacheSidePackets) {
    core_id = _core_id;
    DPRINTF(MAACachePort, "%s: core_id: %d\n", __func__, core_id);
    maxOutstandingCacheSidePackets = _maxOutstandingCacheSidePackets;
    // 16384 is maximum transmitList of PacketQueue (CPU side port of LLC)
    // Taken from gem5-hpc/src/mem/packet_queue.cc (changed from 1024 to 16384)
    maxOutstandingCacheSidePackets = std::min(maxOutstandingCacheSidePackets, 16384);
    // We let it to be 32 less than the maximum
    maxOutstandingCacheSidePackets -= 32;
    blockReason = BlockReason::NOT_BLOCKED;
}

MAA::CacheSidePort::CacheSidePort(const std::string &_name,
                                  MAA *_maa,
                                  const std::string &_label)
    : MAACacheRequestPort(_name, _reqQueue, _snoopRespQueue),
      _reqQueue(*_maa, *this, _snoopRespQueue, _label),
      _snoopRespQueue(*_maa, *this, true, _label), maa(_maa) {
    outstandingCacheSidePackets = 0;
    blockReason = BlockReason::NOT_BLOCKED;
}
} // namespace gem5