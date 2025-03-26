#include "mem/cache/prefetch/diff_matching.hh"
#include "mem/cache/mshr.hh"
#include "mem/cache/base.hh"

#include "debug/DMP.hh"
#include "params/DiffMatchingPrefetcher.hh"

namespace gem5 {

GEM5_DEPRECATED_NAMESPACE(Prefetcher, prefetch);
namespace prefetch {

DiffMatching::DiffMatching(const DiffMatchingPrefetcherParams &p)
    : Stride(p),
      iddt_ent_num(p.iddt_ent_num),
      tadt_ent_num(p.tadt_ent_num),
      iq_ent_num(p.iq_ent_num),
      rg_ent_num(p.rg_ent_num),
      ics_ent_num(p.ics_ent_num),
      rt_ent_num(p.rt_ent_num),
      range_ahead_dist(p.range_ahead_dist),
      indir_range(p.indir_range),
      notify_latency(p.notify_latency),
      cur_range_priority(0),
      range_group_size(p.range_group_size),
      iddt_diff_num(p.iddt_diff_num),
      tadt_diff_num(p.tadt_diff_num),
      indexDataDeltaTable(p.iddt_ent_num, iddt_ent_t(p.iddt_diff_num, false)),
      targetAddrDeltaTable(p.tadt_ent_num, tadt_ent_t(p.tadt_diff_num, false)),
      iddt_ptr(0), tadt_ptr(0),
      range_unit_param(p.range_unit),
      range_level_param(p.range_level),
      rangeTable(p.rg_ent_num * 4, RangeTableEntry(p.range_unit, p.range_level, false)),
      rg_ptr(0),
      indexQueue(p.iq_ent_num),
      iq_ptr(0),
      indirectCandidateScoreboard(p.ics_ent_num, ICSEntry(p.ics_candidate_num, false)),
      ics_ptr(0),
      checkNewIndexEvent([this] { pickIndexPC(); }, this->name()),
      auto_detect(p.auto_detect),
      detect_period(p.detect_period),
      ics_miss_threshold(p.ics_miss_threshold),
      ics_candidate_num(p.ics_candidate_num),
      relationTable(p.rt_ent_num),
      rt_ptr(0),
      statsDMP(this),
      pf_helper(nullptr) {
    /**
     * Priority Update Policy: 
     * new range-type priority = cur.priority - range_group_size
     * new single-type priority = parent_rte.priority + 1
    */

    // init cur_range_priority
    cur_range_priority = std::numeric_limits<int32_t>::max();
    cur_range_priority -= cur_range_priority % range_group_size;

    if (!p.auto_detect) {

        /**
         * Manual Mode
        */
        std::vector<Addr> pc_list;

        // init IDDT
        if (!p.index_pc_init.empty()) {
            for (auto index_pc : p.index_pc_init) {
                indexDataDeltaTable[iddt_ptr].update(index_pc, 0, 0).validate();
                iddt_ptr++;
            }
            pc_list.insert(
                pc_list.end(), p.index_pc_init.begin(), p.index_pc_init.end());
        }

        // init TADT
        if (!p.target_pc_init.empty()) {
            for (auto target_pc : p.target_pc_init) {
                targetAddrDeltaTable[tadt_ptr].update(target_pc, 0, 0).validate();
                tadt_ptr++;
            }
            pc_list.insert(
                pc_list.end(), p.target_pc_init.begin(), p.target_pc_init.end());
        }

        // init RangeTable
        if (!p.range_pc_init.empty()) {
            for (auto range_pc : p.range_pc_init) {
                for (unsigned int shift_try : shift_v) {
                    rangeTable[rg_ptr].update(range_pc, 0x0, shift_try, 0).validate();
                    rg_ptr++;
                }
            }
        }

        std::sort(pc_list.begin(), pc_list.end());
        pc_list.erase(std::unique(pc_list.begin(), pc_list.end()), pc_list.end());
        statsDMP.regStatsPerPC(pc_list);
        dmp_stats_pc = pc_list;
    }
}

DiffMatching::~DiffMatching() {
}

DiffMatching::DMPStats::DMPStats(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(dmp_pfIdentified, statistics::units::Count::get(),
               "number of DMP prefetch candidates identified"),
      ADD_STAT(dmp_pfIdentifiedPerPfPC, statistics::units::Count::get(),
               "number of DMP prefetch candidates identified"),
      ADD_STAT(dmp_noValidDataPerPC, statistics::units::Count::get(),
               "number of DMP prefetch candidates identified"),
      ADD_STAT(dmp_dataFill, statistics::units::Count::get(),
               "number of DMP prefetch candidates identified") {
    using namespace statistics;

    int max_per_pc = 32;

    dmp_pfIdentifiedPerPfPC
        .init(max_per_pc)
        .flags(total | nozero | nonan);
    dmp_noValidDataPerPC
        .init(max_per_pc)
        .flags(total | nozero | nonan);
}

void DiffMatching::DMPStats::regStatsPerPC(const std::vector<Addr> &stats_pc_list) {
    using namespace statistics;

    int max_per_pc = 32;
    assert(stats_pc_list.size() < max_per_pc);

    for (int i = 0; i < stats_pc_list.size(); i++) {
        std::stringstream stream;
        stream << std::hex << stats_pc_list[i];
        std::string pc_name = stream.str();

        dmp_pfIdentifiedPerPfPC.subname(i, pc_name);
        dmp_noValidDataPerPC.subname(i, pc_name);
    }
}

void DiffMatching::pickIndexPC() {
    float cur_weight = std::numeric_limits<float>::min();
    IndexQueueEntry *choosed_ent = nullptr;

    for (auto &iq_ent : indexQueue) {

        if (!iq_ent.valid)
            continue;

        float try_weight = iq_ent.getWeight();
        if (try_weight > cur_weight) {
            cur_weight = try_weight;
            choosed_ent = &iq_ent;
        }
    }

    if (choosed_ent != nullptr) {
        // get the choosed index pc
        choosed_ent->tried++;
        insertICS(choosed_ent->index_pc, choosed_ent->cID);
        insertIDDT(choosed_ent->index_pc, choosed_ent->cID);

        DPRINTF(
            DMP, "pick for ICS: indexPC %llx cID %d\n",
            choosed_ent->index_pc, choosed_ent->cID);
    }

    // schedule next try
    if (auto_detect) {
        schedule(checkNewIndexEvent, curTick() + clockPeriod() * detect_period);
    }
}

void DiffMatching::matchUpdate(Addr index_pc_in, Addr target_pc_in, ContextID cID_in) {
    // update IndexQueueEntry matched
    for (auto &iq_ent : indexQueue) {

        if (!iq_ent.valid)
            continue;

        if (iq_ent.index_pc == index_pc_in && iq_ent.cID == cID_in) {
            iq_ent.matched++;
            break;
        }
    }

    // insert matched target pc as new index pc
    insertIndexQueue(target_pc_in, cID_in);
}

bool DiffMatching::ICSEntry::updateMiss(Addr miss_pc, int miss_thred) {
    auto candidate = miss_count.find(miss_pc);

    if (candidate != miss_count.end()) {

        if (candidate->second >= miss_thred) {
            // should send miss pc to TADT
            return true;
        } else {
            candidate->second++;
        }

    } else if (miss_count.size() < candidate_num) {
        // append a new candidate
        miss_count.insert({miss_pc, 0});
    }

    return false;
}

void DiffMatching::notifyICSMiss(Addr miss_addr, Addr miss_pc_in, ContextID cID_in) {
    for (auto &ics_ent : indirectCandidateScoreboard) {

        if (!ics_ent.valid)
            continue;

        if (ics_ent.cID != cID_in)
            continue;

        DPRINTF(DMP, "ICS updateMiss: targetPC %llx Addr %llx cID %d\n", miss_pc_in, miss_addr, cID_in);
        if (ics_ent.updateMiss(miss_pc_in, ics_candidate_num)) {
            // try insert new entry to TADT and RangeTable
            // IDDT entry should be inserted by IndexQueue
            insertTADT(miss_pc_in, cID_in);
            insertRG(miss_addr, miss_pc_in, cID_in);

            DPRINTF(DMP, "ICS select: targetPC %llx cID %d\n", miss_pc_in, cID_in);

            return;
        }
    }
}

void DiffMatching::insertIndexQueue(Addr index_pc_in, ContextID cID_in) {
    // check if already exist
    for (auto &iq_ent : indexQueue) {

        if (!iq_ent.valid)
            continue;

        if (iq_ent.index_pc == index_pc_in && iq_ent.cID == cID_in)
            return;
    }

    // insert to position iq_ptr
    indexQueue[iq_ptr].update(index_pc_in, cID_in).validate();
    iq_ptr = (iq_ptr + 1) % iq_ent_num;

    DPRINTF(DMP, "insert indexQueue: indexPC %llx cID %d\n", index_pc_in, cID_in);
}

void DiffMatching::insertICS(Addr index_pc_in, ContextID cID_in) {
    // check if already exist
    for (auto &ics_ent : indirectCandidateScoreboard) {

        if (!ics_ent.valid)
            continue;

        if (ics_ent.index_pc == index_pc_in && ics_ent.cID == cID_in)
            return;
    }

    // insert to position ics_ptr
    indirectCandidateScoreboard[ics_ptr].update(index_pc_in, cID_in).validate();
    ics_ptr = (ics_ptr + 1) % ics_ent_num;

    DPRINTF(DMP, "insert ICS: indexPC %llx cID %d\n", index_pc_in, cID_in);
}

void DiffMatching::insertIDDT(Addr index_pc_in, ContextID cID_in) {
    // check if already exist
    for (auto &iddt_ent : indexDataDeltaTable) {

        if (!iddt_ent.isValid())
            continue;

        if (iddt_ent.getPC() == index_pc_in && iddt_ent.getContextId() == cID_in)
            return;
    }

    // insert to position iddt_ptr
    indexDataDeltaTable[iddt_ptr].update(index_pc_in, cID_in).validate();
    iddt_ptr = (iddt_ptr + 1) % iddt_ent_num;

    DPRINTF(DMP, "insert IDDT: indexPC %llx cID %d\n", index_pc_in, cID_in);
}

void DiffMatching::insertTADT(Addr target_pc_in, ContextID cID_in) {
    // check if already exist
    for (auto &tadt_ent : targetAddrDeltaTable) {

        if (!tadt_ent.isValid())
            continue;

        if (tadt_ent.getPC() == target_pc_in && tadt_ent.getContextId() == cID_in)
            return;
    }

    // insert to position tadt_ptr
    targetAddrDeltaTable[tadt_ptr].update(target_pc_in, cID_in).validate();
    tadt_ptr = (tadt_ptr + 1) % tadt_ent_num;

    DPRINTF(DMP, "insert TADT: targetPC %llx cID %d\n", target_pc_in, cID_in);
}

void DiffMatching::insertRG(Addr req_addr_in, Addr target_pc_in, ContextID cID_in) {
    // check if already exist
    for (auto &rg_ent : rangeTable) {

        if (!rg_ent.valid)
            continue;

        if (rg_ent.target_pc == target_pc_in && rg_ent.cID == cID_in)
            return;
    }

    // insert 4 rangeTableRntry for different shift values
    for (auto shift_try : shift_v) {
        rangeTable[rg_ptr].update(
                              target_pc_in, req_addr_in, shift_try, cID_in)
            .validate();
        rg_ptr = (rg_ptr + 1) % (rg_ent_num * 4);
    }

    DPRINTF(DMP, "insert RG: targetPC %llx cID %d\n", target_pc_in, cID_in);
}

void DiffMatching::diffMatching(const tadt_ent_t &tadt_ent) {
    // ready flag check
    assert(tadt_ent.isValid() && tadt_ent.isReady());

    ContextID tadt_ent_cID = tadt_ent.getContextId();

    // try to match all valid and ready index data diff-sequence
    for (const auto &iddt_ent : indexDataDeltaTable) {
        if (!iddt_ent.isValid() || !iddt_ent.isReady())
            continue;
        if (tadt_ent_cID != iddt_ent.getContextId())
            continue;

        // a specific index data diff-sequence may have multiple matching point
        for (int i_start = 0; i_start < iddt_diff_num - tadt_diff_num + 1; i_start++) {

            // try different shift values
            for (unsigned int shift_try : shift_v) {
                int t_start = 0;
                while (t_start < tadt_diff_num) {
                    if (iddt_ent[i_start + t_start] != (tadt_ent[t_start] >> shift_try)) {
                        break;
                    }
                    t_start++;
                }

                if (t_start == tadt_diff_num) {
                    // match success
                    // insert pattern to RelationTable
                    insertRT(iddt_ent, tadt_ent, i_start + tadt_diff_num, shift_try, tadt_ent_cID);

                    // match updata
                    matchUpdate(iddt_ent.getPC(), tadt_ent.getPC(), tadt_ent.getContextId());
                }
            }
        }
    }
}

bool DiffMatching::findRTE(Addr index_pc, Addr target_pc, ContextID cID) {
    for (const auto &rte : relationTable) {
        if (!rte.valid)
            continue;

        // only allow one index for each target
        if (rte.target_pc == target_pc &&
            rte.cID == cID) {
            return true;
        }

        // avoid ring
        if (rte.index_pc == target_pc &&
            rte.target_pc == index_pc &&
            rte.cID == cID) {
            return true;
        }
    }
    return false;
}

bool DiffMatching::checkRedundantRTE(Addr index_pc, Addr target_base_addr, ContextID cID) {
    // check whether current new RTE will be prefetched by other exist RTEs
    Addr block_addr_mask = ~(Addr(blkSize - 1));
    for (const auto &rte : relationTable) {
        if (!rte.valid)
            continue;

        if (rte.index_pc == index_pc &&
            ((rte.target_base_addr ^ target_base_addr) & block_addr_mask) == 0 &&
            rte.cID == cID) {
            // target_base_addr points to the same cache block
            return true;
        }
    }

    return false;
}

void DiffMatching::insertRT(
    const iddt_ent_t &iddt_ent_match,
    const tadt_ent_t &tadt_ent_match,
    int iddt_match_point, unsigned int shift, ContextID cID) {
    Addr new_index_pc = iddt_ent_match.getPC();
    Addr new_target_pc = tadt_ent_match.getPC();

    // check if pattern already exist
    if (findRTE(new_index_pc, new_target_pc, cID))
        return;

    // calculate the target base address
    IndexData data_match = iddt_ent_match.getLast();
    for (int i = iddt_match_point; i < iddt_diff_num; i++) {
        data_match -= iddt_ent_match[i];
    }

    TargetAddr addr_match = tadt_ent_match.getLast();

    TargetAddr base_addr_tmp = addr_match - (data_match << shift);

    DPRINTF(DMP, "Matched: LastData %llx Data %llx Addr %llx Shift %d\n",
            iddt_ent_match.getLast(), data_match, addr_match, shift);

    if (base_addr_tmp > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        printf("Warning: %ld > %ld (LastData %ld Data %ld Addr %ld Shift %d)\n",
               base_addr_tmp, static_cast<uint64_t>(std::numeric_limits<int64_t>::max()),
               iddt_ent_match.getLast(), data_match, addr_match, shift);
        return;
    }

    Addr target_base_addr = static_cast<uint64_t>(base_addr_tmp);

    if (checkRedundantRTE(new_index_pc, target_base_addr, cID))
        return;

    /* get indexPC Range type */
    bool new_range_type;

    // Stride PC should be classified as Range
    // search for all requestor
    if (pf_helper) {
        new_range_type = pf_helper->checkStride(new_index_pc);
    } else {
        new_range_type = this->checkStride(new_index_pc);

        // tmp: only for test
        //new_range_type = true;
    }

    // try tadt's range detection
    if (!new_range_type) {

        // check rangeTable for range type
        for (auto range_ent : rangeTable) {
            if (range_ent.target_pc != new_index_pc || range_ent.cID != cID)
                continue;

            new_range_type = new_range_type || range_ent.getRangeType();

            if (new_range_type == true)
                break;
        }
    }

    /* get priority */
    int32_t priority = 0;
    if (new_range_type) {
        priority = cur_range_priority;
        cur_range_priority -= range_group_size;
    } else {
        priority = getPriority(new_index_pc, cID) + 1;
        assert(priority % range_group_size > 0);
    }

    DPRINTF(DMP, "Insert RelationTable: "
                 "indexPC %llx targetPC %llx target_addr %llx shift %d cID %d rangeType %d priority %d\n",
            new_index_pc, new_target_pc, target_base_addr, shift, cID, new_range_type, priority);

    relationTable[rt_ptr].update(
                             new_index_pc,
                             new_target_pc,
                             target_base_addr,
                             shift,
                             new_range_type,
                             indir_range, // TODO: dynamic detection
                             cID,
                             true,
                             priority)
        .validate();
    rt_ptr = (rt_ptr + 1) % rt_ent_num;
}

int32_t
DiffMatching::getPriority(Addr pc_in, ContextID cID_in) {
    int32_t priority = 0;
    for (auto &rt_ent : relationTable) {
        // if (!rt_ent.valid()) continue;

        if (rt_ent.target_pc != pc_in)
            continue;

        if (cID_in != -1 && rt_ent.cID != cID_in)
            continue;

        priority = rt_ent.priority;
        break;
    }

    return priority;
}

bool DiffMatching::RangeTableEntry::updateSample(Addr addr_in) {
    // continuity check
    // assert(target_PC == PC_in);

    Addr addr_shifted = addr_in >> shift_times;

    // repeation check
    if (addr_shifted == cur_tail[0] || addr_shifted == cur_tail[1]) {
        return false;
    }

    // continuity check
    if (addr_shifted == cur_tail[0] + 1) {
        cur_count++;

        cur_tail[1] = cur_tail[0];
        cur_tail[0] = addr_shifted;

        return false;
    }

    if (cur_count > 0) {
        // generate a range sample to range count
        int sampled_level;
        if (cur_count >= range_quant_level * range_quant_unit) {
            sampled_level = range_quant_level;
        } else {
            sampled_level = (cur_count + range_quant_unit) / range_quant_unit;
        }
        sample_count[sampled_level - 1]++;
        cur_count = 0;
    }

    cur_tail[1] = cur_tail[0];
    cur_tail[0] = addr_shifted;

    return true;
}

bool DiffMatching::RangeTableEntry::getRangeType() const {
    int sum = 0;
    for (int sample : sample_count) {
        sum += sample;
    }
    return (sum > 0);
}

bool DiffMatching::rangeFilter(Addr pc_in, Addr addr_in, ContextID cID_in) {
    bool ret = true;

    for (auto &range_ent : rangeTable) {

        if (!range_ent.valid)
            continue;

        if (range_ent.target_pc != pc_in || range_ent.cID != cID_in)
            continue;

        DPRINTF(DMP, "updateSample: pc %llx addr %llx cur_tail %llx\n",
                pc_in, addr_in, range_ent.cur_tail);
        bool update_ret = range_ent.updateSample(addr_in);
        ret = ret && update_ret;
    }

    return ret;
}

void DiffMatching::notifyL1Req(const CacheAccessProbeArg &arg) {
    // only process with read request
    PacketPtr pkt = arg.pkt;

    if (!pkt->isRead())
        return;

    // update TADT
    if (!pkt->req->hasPC() || !pkt->req->hasVaddr()) {
        return;
    }

    Addr req_addr = pkt->req->getVaddr();

    // avoid overflow when calculating DiffSeq
    if (req_addr > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        return;

    for (auto &tadt_ent : targetAddrDeltaTable) {

        Addr target_pc = tadt_ent.getPC();

        // tadt_ent validation check
        if (target_pc != pkt->req->getPC() || !tadt_ent.isValid())
            continue;

        // range check
        if (!rangeFilter(target_pc, req_addr,
                         pkt->req->hasContextId() ? pkt->req->contextId() : 0))
            continue;

        DPRINTF(DMP, "notifyL1Req: [filter pass] PC %llx, cID %d, Addr %llx, PAddr %llx, VAddr %llx\n",
                pkt->req->hasPC() ? pkt->req->getPC() : 0x0,
                pkt->req->hasContextId() ? pkt->req->contextId() : 0,
                pkt->getAddr(),
                pkt->req->getPaddr(),
                pkt->req->hasVaddr() ? pkt->req->getVaddr() : 0x0);

        // check passed, fill in
        tadt_ent.fill(
            static_cast<TargetAddr>(pkt->req->getVaddr()),
            pkt->req->hasContextId() ? pkt->req->contextId() : 0);

        // try matching
        if (tadt_ent.isReady()) {
            DPRINTF(DMP, "try diffMatching for target PC: %llx\n", target_pc);
            diffMatching(tadt_ent);
        }
    }

    DPRINTF(DMP, "notifyL1Req: PC %llx, Addr %llx, PAddr %llx, VAddr %llx\n",
            pkt->req->hasPC() ? pkt->req->getPC() : 0x0,
            pkt->getAddr(),
            pkt->req->getPaddr(),
            pkt->req->hasVaddr() ? pkt->req->getVaddr() : 0x0);
}

void DiffMatching::notifyL1Resp(const CacheAccessProbeArg &arg) {
    PacketPtr pkt = arg.pkt;

    if (!pkt->req->hasPC()) {
        DPRINTF(DMP, "notifyL1Resp: no PC\n");
        return;
    }

    if (!pkt->validData()) {
        DPRINTF(DMP, "notifyL1Resp: PC %llx, PAddr %llx, no Data, %s\n",
                pkt->req->getPC(), pkt->req->getPaddr(), pkt->cmdString());
        return;
    }

    /** 
     * NOTE: assume the max length of resp data is 8 byte (int64)
     * Since pkt only keeps the data which req needs, we should 
     * parse all data. 
    **/
    const int data_stride = 8;
    const int byte_width = 8;

    if (pkt->getSize() > 8)
        return;
    uint8_t data[8] = {0};
    pkt->writeData(data);
    uint64_t resp_data = 0;
    for (int i_st = data_stride - 1; i_st >= 0; i_st--) {
        resp_data = resp_data << byte_width;
        resp_data += static_cast<uint64_t>(data[i_st]);
    }

    // avoid overflow when calculating DiffSeq
    if (resp_data > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        return;

    // update IDDT
    for (auto &iddt_ent : indexDataDeltaTable) {
        if (iddt_ent.getPC() == pkt->req->getPC() && iddt_ent.isValid()) {

            IndexData new_data;
            std::memcpy(&new_data, &resp_data, sizeof(int64_t));

            // repeation check
            if (iddt_ent.getLast() == new_data)
                continue;

            DPRINTF(DMP, "notifyL1Resp: [filter pass] PC %llx, PAddr %llx, VAddr %llx, Size %d, Data %llx\n",
                    pkt->req->getPC(), pkt->req->getPaddr(),
                    pkt->req->hasVaddr() ? pkt->req->getVaddr() : 0x0,
                    pkt->getSize(), resp_data);

            iddt_ent.fill(new_data, pkt->req->hasContextId() ? pkt->req->contextId() : 0);
        }
    }

    // DPRINTF(DMP, "notifyL1Resp: PC %llx, PAddr %llx, VAddr %llx, Size %d, Data %llx\n",
    //                     pkt->req->getPC(), pkt->req->getPaddr(),
    //                     pkt->req->hasVaddr() ? pkt->req->getVaddr() : 0x0,
    //                     pkt->getSize(), resp_data);
}

void DiffMatching::notifyFill(const CacheAccessProbeArg &arg, const uint8_t *data_ptr) {
    PacketPtr pkt = arg.pkt;
    /** use virtual address for prefetch */
    assert(mmu != nullptr);

    if (!pkt->req->hasPC()) {
        // DPRINTF(DMP, "notifyFill: no PC\n");
        return;
    }

    if (!pkt->validData()) {
        // DPRINTF(DMP, "notifyFill: PC %llx, PAddr %llx, no Data, %s\n",
        //         pkt->req->getPC(), pkt->req->getPaddr(), pkt->cmdString());
        statsDMP.dmp_noValidData++;
        for (int i = 0; i < dmp_stats_pc.size(); i++) {
            Addr req_pc = pkt->req->getPC();
            if (req_pc == dmp_stats_pc[i]) {
                statsDMP.dmp_noValidDataPerPC[i]++;
                break;
            }
        }
        return;
    }

    /* get response data */
    uint8_t fill_data[blkSize];
    std::memcpy(fill_data, data_ptr, blkSize);

    /* prinf response data in bytes */
    if (debug::DMP) {
        unsigned data_offset_debug = pkt->req->getPaddr() & (blkSize - 1);
        do {
            // int64_t resp_data = (int64_t)((uint64_t)fill_data[data_offset_debug] + (((uint64_t)fill_data[data_offset_debug + 1]) << 8) + (((uint64_t)fill_data[data_offset_debug + 2]) << 16) + (((uint64_t)fill_data[data_offset_debug + 3]) << 24));
            // DPRINTF(DMP, "notifyFill: PC %llx, PAddr %llx, DataOffset %d, Data %llx\n",
            //         pkt->req->getPC(), pkt->req->getPaddr(), data_offset_debug, resp_data);
            data_offset_debug += 4;
        } while (data_offset_debug < blkSize);
    }

    Addr pc = pkt->req->getPC();
    for (const auto &rt_ent : relationTable) {

        if (!rt_ent.valid)
            continue;

        if (rt_ent.index_pc != pc)
            continue;

        /* Assume response data is a int and always occupies 4 bytes */
        const int data_stride = 4;
        const int byte_width = 8;

        /* set range_end, only process one data if not range type */
        unsigned range_end;
        unsigned data_offset = pkt->req->getPaddr() & (blkSize - 1);
        if (rt_ent.range) {
            range_end = std::min(data_offset + data_stride * rt_ent.range_degree, blkSize);
        } else {
            range_end = data_offset + data_stride;
        }

        /* loop for range prefetch */
        for (unsigned i_of = data_offset; i_of < range_end; i_of += data_stride) {
            /* integrate fill_data[] to resp_data (considered as unsigned)  */
            uint64_t resp_data = 0;
            for (int i_st = data_stride - 1; i_st >= 0; i_st--) {
                resp_data = resp_data << byte_width;
                resp_data += static_cast<uint64_t>(fill_data[i_of + i_st]);
            }

            /* calculate target prefetch address */
            Addr pf_addr = (resp_data << rt_ent.shift) + rt_ent.target_base_addr;
            // DPRINTF(DMP,
            //         "notifyFill: PC %llx, pkt_addr %llx, pkt_offset %d, pkt_data %d, pf_addr %llx\n",
            //         pc, pkt->getAddr(), data_offset, resp_data, pf_addr);

            // insert to missing translation queue
            insertIndirectPrefetch(pf_addr, rt_ent.target_pc, rt_ent.cID, rt_ent.priority, pkt->getRegion());

            if (rt_ent.target_pc == 0x400ca0) {
                for (int i = 1; i <= range_ahead_dist; i++) {
                    insertIndirectPrefetch(pf_addr + blkSize * i, rt_ent.target_pc, rt_ent.cID, rt_ent.priority, pkt->getRegion());
                }
            }
        }

        // try to do translation immediately
        processMissingTranslations(queueSize - pfq.size());
    }

    statsDMP.dmp_dataFill++;
}

void DiffMatching::insertIndirectPrefetch(Addr pf_addr, Addr target_pc,
                                          ContextID cID, int32_t priority,
                                          int8_t region) {
    Addr blk_pf_addr = blockAddress(pf_addr);

    /** get a fake pfi, generator pc is target_pc for chain-trigger */
    /** use blk-aligned address for repeation check in PFQ */
    PrefetchInfo fake_pfi(blk_pf_addr, target_pc, requestorId, cID);

    statsDMP.dmp_pfIdentified++;
    for (int i = 0; i < dmp_stats_pc.size(); i++) {
        if (target_pc == dmp_stats_pc[i]) {
            statsDMP.dmp_pfIdentifiedPerPfPC[i]++;
            break;
        }
    }

    /* filter repeat request */
    if (queueFilter) {
        if (alreadyInQueue(pfq, fake_pfi, priority)) {
            /* repeat address in pfi */
            return;
        }
        if (alreadyInQueue(pfqMissingTranslation, fake_pfi, priority)) {
            /* repeat address in pfi */
            return;
        }
    }

    /* create pkt and req for dpp, fake for later translation*/
    DeferredPacket dpp(this, fake_pfi, 0, priority, *cache_accessor);
    dpp.region = region;

    /* no need trigger virtual addr for DMP */
    dpp.pfInfo.setPC(target_pc);     // setting target pc
    dpp.pfInfo.setAddr(blk_pf_addr); // setting target virtual addr, blk-aligned

    // TODO: should set ContextID

    /* make translation request and set PREFETCH flag*/
    RequestPtr translation_req = std::make_shared<Request>(
        pf_addr, blkSize, Request::PREFETCH, requestorId,
        target_pc, cID);

    /* set to-be-translating request, append to pfqMissingTranslation*/
    dpp.setTranslationRequest(translation_req);
    dpp.tc = system->threads[translation_req->contextId()];

    // pf_time will not be set until translation completes

    addToQueue(pfqMissingTranslation, dpp);
    DPRINTF(DMP, "DMP ADDR: 0x%lx, PC: 0x%lx, cID: %d added to queue\n", pf_addr, target_pc, cID);
}

void DiffMatching::notify(const CacheAccessProbeArg &arg, const PrefetchInfo &pfi) {
    PacketPtr pkt = arg.pkt;
    if (pfi.isCacheMiss()) {
        // Miss
        DPRINTF(DMP, "notify::CacheMiss: PC %llx, Addr %llx, PAddr %llx, VAddr %llx\n",
                pkt->req->hasPC() ? pkt->req->getPC() : 0x0,
                pkt->getAddr(),
                pkt->req->getPaddr(),
                pkt->req->hasVaddr() ? pkt->req->getVaddr() : 0x0);
        notifyICSMiss(
            pkt->req->hasVaddr() ? pkt->req->getVaddr() : 0x0,
            pkt->req->hasPC() ? pkt->req->getPC() : 0x0,
            pkt->req->hasContextId() ? pkt->req->contextId() : 0);

    } else {
        // Hit
        DPRINTF(DMP, "notify::CacheHit: PC %llx, Addr %llx, PAddr %llx, VAddr %llx\n",
                pkt->req->hasPC() ? pkt->req->getPC() : 0x0,
                pkt->getAddr(),
                pkt->req->getPaddr(),
                pkt->req->hasVaddr() ? pkt->req->getVaddr() : 0x0);
    }
    // if (pkt->req->hasPC() && pkt->req->hasContextId()) {
    //     for (auto& rt_ent: relationTable) {
    //         if (rt_ent.index_pc == pkt->req->getPC())
    //         {
    //             rt_ent.cID = pkt->req->contextId();
    //         }
    //     }
    //     // DPRINTF(DMP, "notify: Request Flags %llx ContextID %d\n", pkt->req->getFlags(), pkt->req->contextId());
    // }

    assert(pkt->isRequest());

    // TODO: Should we do further prefetch for high level cache prefetch ?
    // e.g. L1 Prefetch Request access and hit at L2.
    // currently L1 DMP Request will be translated to ReadShared request at L2.

    //if (!pkt->req->isPrefetch()) {
    // Test again in Cache which prefetch send to, in case ppMiss->notify() from other position.
    // When this called by ppHit->notify(), we use cache blk data to prefetch.
    CacheBlk *try_cache_blk = cache_accessor->getCacheBlk(pkt->getAddr(), pkt->isSecure());

    // assert(try_cache_blk && try_cache_blk->data);

    if (try_cache_blk != nullptr && try_cache_blk->data) {
        notifyFill(arg, try_cache_blk->data);
    }
    //}

    // CacheBlk* try_miss_blk = cache_accessor->getCacheBlk(pkt->getAddr(), pkt->isSecure());

    // if (try_miss_blk != nullptr) {
    //     assert(try_miss_blk && try_miss_blk->data);
    //     notifyFill(pkt, try_miss_blk->data);

    //     DPRINTF(DMP, "notify:: Hit at prefetcher's cache, try DMP prefetch.\n");
    // }

    Queued::notify(arg, pfi);
}

void DiffMatching::callReadytoIssue(const PrefetchInfo &pfi) {
    Addr pc = pfi.getPC();

    // mask kernel space
    if ((pc & 0xffff800000000000) == 0) {
        insertIndexQueue(pc, pfi.getcID());
    }
    if (auto_detect && !checkNewIndexEvent.scheduled()) {
        // schedule next index pick
        schedule(checkNewIndexEvent, curTick() + clockPeriod() * detect_period);
    }
}

void DiffMatching::addPfHelper(Stride *s) {
    fatal_if(pf_helper != nullptr, "Only one PfHelper can be registered");
    pf_helper = s;
}

void DiffMatching::calculatePrefetch(const PrefetchInfo &pfi,
                                     std::vector<AddrPriority> &addresses,
                                     const CacheAccessor &cache) {
    if (pf_helper) {
        // use fake_addresses to drop Stride Prefetch while keep updating pcTables
        std::vector<AddrPriority> fake_addresses;

        Stride::calculatePrefetch(pfi, fake_addresses, cache);
    } else {
        Stride::calculatePrefetch(pfi, addresses, cache);
    }

    // set priority for stride prefetch
    // in case the stride pc is the same as rt_ent's target pc
    int32_t priority = 0;
    if (pfi.hasPC()) {
        priority = getPriority(pfi.getPC(), -1);
    }

    for (auto &addr : addresses) {
        addr.second = priority;
    }
}

} // namespace prefetch

} // namespace gem5
