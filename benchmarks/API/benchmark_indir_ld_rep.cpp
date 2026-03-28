#include "MAA.hpp"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#if !defined(FUNC) && !defined(GEM5) && !defined(GEM5_MAGIC)
#define FUNC
#endif

#if defined(FUNC)
#include "MAA_functional.hpp"
#elif defined(GEM5)
#include "MAA_gem5.hpp"
#include <gem5/m5ops.h>
#elif defined(GEM5_MAGIC)
#include "MAA_gem5_magic.hpp"
#endif

static volatile uint64_t global_sink = 0;

inline void prime_tile(int tile_id, uint16_t size) {
    SPD_size_noncacheable[tile_id] = size;
    SPD_ready_noncacheable[tile_id] = 1;
#ifdef GEM5
    __asm__ __volatile__("mfence;");
#endif
}

// build deterministic chain for each lane
void init_pointer_chains(uint64_t *backing, uint64_t *starts, uint32_t *reps, int n, int depth) {
    assert(depth >= 1);
    for (int i = 0; i < n; i++) {
        int lane_base = i * (depth + 1);
        for (int hop = 0; hop < depth; hop++) {
            uint64_t *node = &backing[lane_base + hop];
            uint64_t *next = &backing[lane_base + hop + 1];
            *node = reinterpret_cast<uint64_t>(next);
        }
        backing[lane_base + depth] = static_cast<uint64_t>(i * 17 + 5);
        starts[i] = reinterpret_cast<uint64_t>(&backing[lane_base]);
        reps[i] = static_cast<uint32_t>(depth);
    }
}

// baseline path
void indir_ld_rep_baseline(uint64_t *out, const uint64_t *starts, const uint32_t *reps, int n) {
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
    for (int i = 0; i < n; i++) {
        uint64_t current = starts[i];
        for (uint32_t hop = 0; hop < reps[i]; hop++) {
            current = *reinterpret_cast<uint64_t *>(current);
        }
        out[i] = current;
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void indir_ld_rep_maa(uint64_t *out, uint64_t *backing, const uint64_t *starts, const uint32_t *reps, int n) {
    init_MAA();

    int addr_tile = get_new_tile<uint64_t>();
    int rep_tile = get_new_tile<uint32_t>();
    int out_tile = get_new_tile<uint64_t>();

    uint64_t *addr_tile_ptr = get_cacheable_tile_pointer<uint64_t>(addr_tile);
    uint32_t *rep_tile_ptr = get_cacheable_tile_pointer<uint32_t>(rep_tile);

    for (int i = 0; i < n; i++) {
        addr_tile_ptr[i] = starts[i];
        rep_tile_ptr[i] = reps[i];
    }
    prime_tile(addr_tile, n);
    prime_tile(rep_tile, n);

#ifdef GEM5
    m5_work_begin(1, 0);
    m5_reset_stats(0, 0);
#endif
    maa_indirect_load_rep<uint64_t>(backing, addr_tile, out_tile, rep_tile);
    wait_ready(out_tile);
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(1, 0);
#endif

    uint64_t *out_tile_ptr = get_cacheable_tile_pointer<uint64_t>(out_tile);
    for (int i = 0; i < n; i++) {
        out[i] = out_tile_ptr[i];
    }
}

uint64_t checksum(const uint64_t *data, int n) {
    uint64_t sum = 0;
    for (int i = 0; i < n; i++) {
        sum ^= data[i] + 0x9e3779b97f4a7c15ULL + (sum << 6) + (sum >> 2);
    }
    return sum;
}

bool compare_arrays(const uint64_t *expected, const uint64_t *actual, int n) {
    for (int i = 0; i < n; i++) {
        if (expected[i] != actual[i]) {
            std::cout << "Mismatch at " << i << ": expected " << expected[i]
                      << ", got " << actual[i] << std::endl;
            return false;
        }
    }
    return true;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cout << "Usage: " << argv[0] << " <n> <depth> [BASE|MAA|CMP]" << std::endl;
        return 1;
    }

    int n = std::stoi(argv[1]);
    int depth = std::stoi(argv[2]);
    std::string mode = argv[3];
    bool run_base = mode == "BASE" || mode == "CMP";
    bool run_maa = mode == "MAA" || mode == "CMP";
    if ((!run_base && !run_maa) || n <= 0 || n > TILE_SIZE || depth <= 0) {
        std::cout << "Usage: " << argv[0] << " <n> <depth> [BASE|MAA|CMP]" << std::endl;
#ifdef GEM5
        m5_exit(1);
#endif
        return 1;
    }

    uint64_t *backing = static_cast<uint64_t *>(malloc(sizeof(uint64_t) * n * (depth + 1)));
    uint64_t *starts = static_cast<uint64_t *>(malloc(sizeof(uint64_t) * n));
    uint32_t *reps = static_cast<uint32_t *>(malloc(sizeof(uint32_t) * n));
    uint64_t *base_out = static_cast<uint64_t *>(malloc(sizeof(uint64_t) * n));
    uint64_t *maa_out = static_cast<uint64_t *>(malloc(sizeof(uint64_t) * n));
    if (backing == nullptr || starts == nullptr || reps == nullptr || base_out == nullptr || maa_out == nullptr) {
        std::cout << "Allocation failed" << std::endl;
        free(backing);
        free(starts);
        free(reps);
        free(base_out);
        free(maa_out);
#ifdef GEM5
        m5_exit(1);
#endif
        return 1;
    }

    init_pointer_chains(backing, starts, reps, n, depth);
    for (int i = 0; i < n; i++) {
        base_out[i] = 0;
        maa_out[i] = 0;
    }

#if defined(FUNC)
    alloc_MAA();
#else
    if (run_maa) {
        alloc_MAA();
    }
#endif

#if defined(FUNC)
    clear_mem_region();
    m5_add_mem_region(backing, backing + n * (depth + 1), 0);
#elif defined(GEM5)
    m5_clear_mem_region();
    m5_add_mem_region(backing, backing + n * (depth + 1), 0);
    if (run_maa) {
        m5_add_mem_region(starts, starts + n, 1);
        m5_add_mem_region(reps, reps + n, 2);
        m5_add_mem_region(base_out, base_out + n, 3);
        m5_add_mem_region(maa_out, maa_out + n, 4);
    }
#endif

    if (run_base) {
        indir_ld_rep_baseline(base_out, starts, reps, n);
        uint64_t base_sum = checksum(base_out, n);
        global_sink ^= base_sum;
        std::cout << "baseline checksum " << base_sum << std::endl;
    }

    if (run_maa) {
        indir_ld_rep_maa(maa_out, backing, starts, reps, n);
        uint64_t maa_sum = checksum(maa_out, n);
        global_sink ^= maa_sum;
        std::cout << "maa checksum " << maa_sum << std::endl;
    }

    bool passed = true;
    if (mode == "CMP") {
        passed = compare_arrays(base_out, maa_out, n);
        if (passed) {
            std::cout << "indir_ld_rep benchmark outputs match" << std::endl;
        }
    }

    free(backing);
    free(starts);
    free(reps);
    free(base_out);
    free(maa_out);

#ifdef GEM5
    m5_exit(passed ? 0 : 1);
#endif
    return passed ? 0 : 1;
}
