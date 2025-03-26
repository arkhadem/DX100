/**
* Difference-based prefetcher
*/

#ifndef __MEM_CACHE_PREFETCH_DIFF_MATCHING_HH__
#define __MEM_CACHE_PREFETCH_DIFF_MATCHING_HH__

#include <vector>
#include <queue>
#include <unordered_map>

#include "base/types.hh"
#include "mem/cache/prefetch/stride.hh"
#include "mem/cache/prefetch/queued.hh"
#include "sim/eventq.hh"

namespace gem5 {

struct DiffMatchingPrefetcherParams;

namespace prefetch {

class DiffMatching : public Stride {

    typedef int64_t IndexData;
    typedef int64_t TargetAddr;

    const int iddt_ent_num;
    const int tadt_ent_num;
    const int iq_ent_num;
    const int rg_ent_num;
    const int ics_ent_num;
    const int rt_ent_num;

    // indirect range prefetch length
    int range_ahead_dist;
    int indir_range;

    int notify_latency;

    // priority init
    int32_t cur_range_priority;
    int32_t range_group_size;

    // possiable shift values
    const unsigned int shift_v[4] = {0, 1, 2, 3};

    /** IDDT and TDDT */
    const int iddt_diff_num;
    const int tadt_diff_num;

    template <typename T>
    class DiffSeqCollection {
        Addr pc;
        bool valid;
        bool ready;
        ContextID cID;
        T last;

        int diff_ptr;
        const int diff_size;
        std::vector<T> diff;

    public:
        // normal constructor
        DiffSeqCollection(Addr pc, T last, int diff_size)
            : pc(pc), valid(false), ready(false), cID(0),
              last(last), diff_ptr(0), diff_size(diff_size) {
            diff.reserve(diff_size);
        };

        // init constructor
        DiffSeqCollection(int diff_size, bool valid = false)
            : valid(valid), diff_size(diff_size) {
            diff.reserve(diff_size);
        };

        ~DiffSeqCollection() = default;

        void validate() { valid = true; };

        void invalidate() {
            diff_ptr = 0;
            cID = 0;
            valid = false;
            diff.clear();
        };

        void fill(T last_in, ContextID cID_in) {
            if (cID_in != cID)
                return;

            if (ready) {
                diff[diff_ptr] = last_in - last;
                diff_ptr = (diff_ptr + 1) % diff_size;
            } else {
                diff.push_back(last_in - last);
                if (diff.size() == diff_size)
                    ready = true;
            }
            last = last_in;
        };

        bool isReady() const { return ready; };

        bool isValid() const { return valid; };

        Addr getPC() const { return pc; };

        ContextID getContextId() const { return cID; };

        T getLast() const { return last; };

        T operator[](int index) const { return diff[(diff_ptr + index) % diff_size]; };

        DiffSeqCollection &update(Addr pc_new, ContextID cID_new, T last_new = 0) {
            pc = pc_new;
            last = last_new;
            cID = cID_new;
            ready = false;
            valid = false;
            diff_ptr = 0;
            diff.clear();
            return *this;
        };
    };

    typedef DiffSeqCollection<IndexData> iddt_ent_t;
    typedef DiffSeqCollection<TargetAddr> tadt_ent_t;

    std::vector<iddt_ent_t> indexDataDeltaTable;
    std::vector<tadt_ent_t> targetAddrDeltaTable;

    int iddt_ptr;
    int tadt_ptr;

    void insertIDDT(Addr index_pc_in, ContextID cID_in);
    void insertTADT(Addr target_pc_in, ContextID cID_in);

    /** RangeTable related */

    /** Range quantification method
    * eg. unit=8, level=4
    * level:   |  1  |  2  |  3  |  4  |
    * unti:    |  u  |  u  |  u  |  u  |
    * range:   0     8     16    24    32 
    */
    const int range_unit_param;  // quantify true range to several units
    const int range_level_param; // total levels of range quant unit

    struct RangeTableEntry {
        Addr target_pc; // range base on req address
        Addr cur_tail[2];
        int cur_count;
        ContextID cID;
        bool valid;

        int shift_times; // 0 (byte) / 2 (int) / 3 (double)

        const int range_quant_unit;  // quantify true range to several units
        const int range_quant_level; // total levels of range quant unit

        // NOTE: Range prefetch distence should coorparate wit StreamPrefetch
        // NOTE: [TODO] more suitable RangePrefetc schedule policy
        std::vector<int> sample_count;

        // normal constructor
        RangeTableEntry(
            Addr target_pc, Addr req_addr, int shift_times, int rql, int rqu) : target_pc(target_pc), cur_tail{req_addr, MaxAddr},
                                                                                cur_count(0), cID(0), valid(false), shift_times(shift_times),
                                                                                range_quant_unit(rqu), range_quant_level(rql),
                                                                                sample_count(rql) {}

        // init constructor
        RangeTableEntry(int rqu, int rql, bool valid = false)
            : valid(valid), range_quant_unit(rqu), range_quant_level(rql),
              sample_count(rql){};

        ~RangeTableEntry() = default;

        bool updateSample(Addr addr_in);

        void validate() { valid = true; };

        void invalidate() {
            valid = false;
            std::fill(sample_count.begin(), sample_count.end(), 0);
        };

        bool getRangeType() const;

        int getPredLevel() const {
            return std::distance(sample_count.begin(),
                                 std::max_element(sample_count.begin(), sample_count.end()));
        }

        RangeTableEntry &update(
            Addr target_pc_in,
            Addr req_addr_in,
            int shift_times_in,
            ContextID cID_in) {
            target_pc = target_pc_in;
            cur_tail[0] = req_addr_in;
            cur_tail[1] = MaxAddr;
            cur_count = 0;
            cID = cID_in;
            valid = false;
            shift_times = shift_times_in;
            std::fill(sample_count.begin(), sample_count.end(), 0);
            return *this;
        }
    };

    std::vector<RangeTableEntry> rangeTable;

    int rg_ptr;

    void insertRG(Addr req_addr_in, Addr target_pc_in, ContextID cID_in);

    bool rangeFilter(Addr pc_in, Addr addr_in, ContextID cID_in);

    /** IndexQueue related */
    struct IndexQueueEntry {
        Addr index_pc;
        ContextID cID;
        bool valid;
        int tried;
        int matched;

        // normal constructor
        IndexQueueEntry(Addr pc_in)
            : index_pc(pc_in), cID(0), valid(false),
              tried(0), matched(0){};

        // init constructor
        IndexQueueEntry(bool valid = false){};

        ~IndexQueueEntry() = default;

        float getWeight() const { return (matched + 1) / (tried + 1e-8); };

        void validate() { valid = true; };

        void invalidate() { valid = false; };

        IndexQueueEntry &update(Addr index_pc_in, ContextID cID_in) {
            index_pc = index_pc_in;
            cID = cID_in;
            valid = false;
            tried = 0;
            matched = 0;
            return *this;
        };
    };
    std::vector<IndexQueueEntry> indexQueue;

    int iq_ptr;

    // TODO: insert from Stride Hit
    void insertIndexQueue(Addr index_pc, ContextID cID_in);

    void pickIndexPC();

    void matchUpdate(Addr index_pc_in, Addr target_pc_in, ContextID cID_in);

    /** IndirectCandidateScoreboard related*/
    struct ICSEntry {
        Addr index_pc;
        ContextID cID;
        int candidate_num;
        bool valid;

        std::unordered_map<Addr, int> miss_count;

        // normal constructor
        ICSEntry(Addr index_pc) : cID(0), valid(false){};

        // init constructor
        ICSEntry(int candidate_num, bool valid = false)
            : candidate_num(candidate_num), valid(valid){};

        ~ICSEntry() = default;

        void validate() { valid = true; };

        void invalidate() { valid = false; };

        bool updateMiss(Addr miss_pc, int miss_thred);

        ICSEntry &update(Addr index_pc_in, ContextID cID_in) {
            index_pc = index_pc_in;
            cID = cID_in;
            valid = false;
            miss_count.clear();
            return *this;
        };
    };
    std::vector<ICSEntry> indirectCandidateScoreboard;

    int ics_ptr;

    void notifyICSMiss(Addr miss_addr, Addr miss_pc_in, ContextID cID_in);

    void insertICS(Addr index_pc_in, ContextID cID_in);

    EventFunctionWrapper checkNewIndexEvent;

    bool auto_detect;

    int detect_period;

    int ics_miss_threshold;

    int ics_candidate_num;

    /** RelationTable related */
    struct RTEntry {
        Addr index_pc;
        Addr target_pc;
        Addr target_base_addr;
        unsigned int shift;
        bool range;
        int range_degree;
        ContextID cID;
        bool valid;
        int32_t priority;

        // normal constructor
        RTEntry(
            Addr index_pc, Addr target_pc, Addr target_base_addr,
            unsigned int shift, bool range, int range_degree,
            ContextID cID, int32_t priority) : index_pc(index_pc), target_pc(target_pc), target_base_addr(target_base_addr),
                                               shift(shift), range(range), range_degree(range_degree), cID(cID), valid(false),
                                               priority(priority) {}

        // default constructor
        RTEntry(bool valid = false) : valid(valid){};

        void validate() { valid = true; };

        void invalidate() { valid = false; };

        // update for new relation
        RTEntry &update(
            Addr index_pc_in,
            Addr target_pc_in,
            Addr target_base_addr_in,
            unsigned int shift_in,
            bool range_in,
            int range_degree_in,
            ContextID cID_in,
            bool valid_in,
            int32_t priority_in) {
            index_pc = index_pc_in;
            target_pc = target_pc_in;
            target_base_addr = target_base_addr_in;
            shift = shift_in;
            range = range_in;
            range_degree = range_degree_in;
            cID = cID_in;
            valid = valid_in;
            priority = priority_in;

            return *this;
        };
    };
    std::vector<RTEntry> relationTable;

    // point to the next update position
    int rt_ptr;

    bool findRTE(Addr index_pc, Addr target_pc, ContextID cID);

    bool checkRedundantRTE(Addr index_pc, Addr target_base_addr, ContextID cID);

    void insertRT(
        const iddt_ent_t &iddt_ent_match, const tadt_ent_t &tadt_ent_match,
        int iddt_match_point, unsigned int shift, ContextID cID);

    int32_t getPriority(Addr target_pc, ContextID cID);

    /** DMP specific stats */
    struct DMPStats : public statistics::Group {
        DMPStats(statistics::Group *parent);
        void regStatsPerPC(const std::vector<Addr> &PC_list);

        // STATS
        statistics::Scalar dmp_pfIdentified;
        statistics::Vector dmp_pfIdentifiedPerPfPC;
        statistics::Scalar dmp_noValidData;
        statistics::Vector dmp_noValidDataPerPC;
        statistics::Scalar dmp_dataFill;
    } statsDMP;

    std::vector<Addr> dmp_stats_pc;

    // A StridePrefetcher which helps DMP detection.
    Stride *pf_helper;

    /** DMP functions */

protected:
    void diffMatching(const tadt_ent_t &tadt_ent);

    void callReadytoIssue(const PrefetchInfo &pfi) override;

public:
    DiffMatching(const DiffMatchingPrefetcherParams &p);
    ~DiffMatching();

    // Base notify for Cache access (Hit or Miss)
    void notify(const CacheAccessProbeArg &arg, const PrefetchInfo &pfi) override;

    // Probe DataResp from Memory for prefetch generation
    void notifyFill(const CacheAccessProbeArg &arg, const uint8_t *data_ptr) override;

    // Probe AddrReq to L1 for prefetch detection
    void notifyL1Req(const CacheAccessProbeArg &arg) override;
    // Probe DataResp from L1 for prefetch detection
    void notifyL1Resp(const CacheAccessProbeArg &arg) override;

    void insertIndirectPrefetch(Addr pf_addr, Addr target_pc,
                                ContextID cID, int32_t priority,
                                int8_t region);

    void addPfHelper(Stride *s);

    void calculatePrefetch(const PrefetchInfo &pfi,
                           std::vector<AddrPriority> &addresses,
                           const CacheAccessor &cache) override;
};

} // namespace prefetch
} // namespace gem5

#endif // __MEM_CACHE_PREFETCH_DIFF_MATCHING_HH__