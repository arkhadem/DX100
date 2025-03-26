#include <iostream>
#include <string>
#include <ctime>
#include <vector>
#include <algorithm> // For std::iota and std::fill
#include <cstdlib>   // For rand()
#include <cmath>     // For std::fabs
#include <omp.h>
using namespace std;

#define DATATYPE float

// #define VERIFY

#define DISTANCE_OTEHRS 85000
#define DISTANCE_P2C    80000
#define PADDING_LEN     90000
#define TOLERANCE       1e-3 // Tolerance for floating-point comparisons

std::vector<int> corner_type;
std::vector<int> c_to_z_map;
std::vector<int> c_to_p_map;
std::vector<DATATYPE> point_gradient;
std::vector<DATATYPE> corner_volume;

std::vector<DATATYPE> zone_volume;
std::vector<DATATYPE> zone_gradient;

std::vector<DATATYPE> zone_volume_exp;
std::vector<DATATYPE> zone_gradient_exp;

#if !defined(FUNC) && !defined(GEM5) && !defined(GEM5_MAGIC)
#define GEM5
#endif

#if defined(FUNC)
#include <MAA_functional.hpp>
#elif defined(GEM5)
#include <MAA_gem5.hpp>
#include <gem5/m5ops.h>
#elif defined(GEM5_MAGIC)
#include "MAA_gem5_magic.hpp"
#endif
int tiles0[NUM_CORES], tiles1[NUM_CORES], tiles2[NUM_CORES], tiles3[NUM_CORES], tiles4[NUM_CORES];
int regs0[NUM_CORES], regs1[NUM_CORES], regs2[NUM_CORES];

void gradzatz() {
    int num_corners = corner_type.size();

#ifdef GEM5
    clear_mem_region();
    // Add memory regions for used arrays in this kernel
    add_mem_region(zone_gradient_exp.data(), zone_gradient_exp.data() + zone_gradient_exp.size()); // 6
    add_mem_region(zone_volume_exp.data(), zone_volume_exp.data() + zone_volume_exp.size());       // 7
    add_mem_region(c_to_p_map.data(), c_to_p_map.data() + c_to_p_map.size());                      // 8
    add_mem_region(corner_volume.data(), corner_volume.data() + corner_volume.size());             // 9
    add_mem_region(c_to_z_map.data(), c_to_z_map.data() + c_to_z_map.size());                      // 10
    add_mem_region(corner_type.data(), corner_type.data() + corner_type.size());                   // 11
    add_mem_region(point_gradient.data(), point_gradient.data() + point_gradient.size());          // 12
    std::cout << "ROI Begin" << std::endl;
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
// Accumulate the zone volume
#pragma omp parallel
    {
#pragma omp for
        for (int c = 0; c < num_corners; ++c) {
            if (corner_type[c] < 1)
                continue; // Only operate on interior corners
            int z = c_to_z_map[c];
#pragma omp atomic
            zone_volume_exp[z] += corner_volume[c];
        }

// Accumulate the zone-centered gradient
#pragma omp for
        for (int c = 0; c < num_corners; ++c) {
            if (corner_type[c] < 1)
                continue; // Only operate on interior corners
            int z = c_to_z_map[c];
            int p = c_to_p_map[c];
            double c_z_vol_ratio = corner_volume[c] / zone_volume_exp[z];
#pragma omp atomic
            zone_gradient_exp[z] += point_gradient[p] * c_z_vol_ratio;
        }
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
    std::cout << "ROI Ended" << std::endl;
    m5_exit(0);
#endif
}

void gradzatz_MAA() {
    int num_corners = corner_type.size();

#ifdef GEM5
    clear_mem_region();
    // Add memory regions for used arrays in this kernel
    add_mem_region(zone_gradient.data(), zone_gradient.data() + zone_gradient.size());    // 6
    add_mem_region(zone_volume.data(), zone_volume.data() + zone_volume.size());          // 7
    add_mem_region(c_to_p_map.data(), c_to_p_map.data() + c_to_p_map.size());             // 8
    add_mem_region(corner_volume.data(), corner_volume.data() + corner_volume.size());    // 9
    add_mem_region(c_to_z_map.data(), c_to_z_map.data() + c_to_z_map.size());             // 10
    add_mem_region(corner_type.data(), corner_type.data() + corner_type.size());          // 11
    add_mem_region(point_gradient.data(), point_gradient.data() + point_gradient.size()); // 12
    std::cout << "ROI Begin" << std::endl;
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
#pragma omp parallel
    {
        int reg0, reg1, reg2;
        int tile0, tile2, tile3, tile5, tileCond;
        int omp_thread_id = omp_get_thread_num();
        reg0 = regs0[omp_thread_id];
        reg1 = regs1[omp_thread_id];
        reg2 = regs2[omp_thread_id];
        tile0 = tiles0[omp_thread_id];
        tile2 = tiles1[omp_thread_id];
        tileCond = tiles3[omp_thread_id];
        tile3 = tiles2[omp_thread_id];
        tile5 = tiles4[omp_thread_id];

        maa_const<int>(1, reg2);
        maa_const<int>(num_corners, reg1);
#pragma omp for
        for (int c = 0; c < num_corners; c += TILE_SIZE) {
            maa_const<int>(c, reg0);
            // Step1: Load corner_type
            maa_stream_load<int>(corner_type.data(), reg0, reg1, reg2, tile0);
            // Step2: Perform comparison
            maa_alu_scalar<int>(tile0, reg2, tileCond, Operation_t::GTE_OP);
            // Step3: Load c_to_z_map
            maa_stream_load<int>(c_to_z_map.data(), reg0, reg1, reg2, tile3);
            // Transfer tile3
            // Step4: Load corner_volume[c]
            maa_stream_load<DATATYPE>(corner_volume.data(), reg0, reg1, reg2, tile0);
            // Step5: Accumulate the local zone volume
            maa_indirect_rmw_vector<DATATYPE>(zone_volume.data(), tile3, tile0, Operation_t::ADD_OP, tileCond);
            wait_ready(tile0);
        }

        // Accumulate the zone volume
        // for (int c = 0; c < num_corners; ++c) {
        //     if (corner_type[c] < 1)
        //         continue; // Only operate on interior corners
        //     int z = c_to_z_map[c];
        //     zone_volume[z] += corner_volume[c];
        // }

        // Accumulate the zone-centered gradient
        int *tileCondPtr = get_cacheable_tile_pointer<int>(tileCond);
        DATATYPE *tile5Ptr = get_cacheable_tile_pointer<DATATYPE>(tile5);
        DATATYPE *tile2Ptr = get_cacheable_tile_pointer<DATATYPE>(tile2);
        DATATYPE *tile0Ptr = get_cacheable_tile_pointer<DATATYPE>(tile0);
#pragma omp for
        for (int c = 0; c < num_corners; c += TILE_SIZE) {
            maa_const<int>(c, reg0);
            // Step1: Load corner_type
            maa_stream_load<int>(corner_type.data(), reg0, reg1, reg2, tile0);
            // Step2: Perform comparison
            maa_alu_scalar<int>(tile0, reg2, tileCond, Operation_t::GTE_OP);
            // Step3: Load c_to_z_map
            maa_stream_load<int>(c_to_z_map.data(), reg0, reg1, reg2, tile3, tileCond); //3 -> z
            // Step4: Load c_to_p_map
            maa_stream_load<int>(c_to_p_map.data(), reg0, reg1, reg2, tile5, tileCond); // 5 -> p
            // Step5: Load point_gradient[p]
            maa_indirect_load<DATATYPE>(point_gradient.data(), tile5, tile0, tileCond); // 0 -> point_gradient
            // Transfer: tileCond (assuming step3 is also mapped to the same region)
            // Step6: Load zone_gradient[z]
            maa_indirect_load<DATATYPE>(zone_volume.data(), tile3, tile2, tileCond); // 2 -> zone_volume
            wait_ready(tile2);
            wait_ready(tile0);
            int tile_size = get_tile_size(tileCond);

#pragma omp simd simdlen(4)
            for (int i = 0; i < tile_size; i++) {
                if (tileCondPtr[i] == 1) {
                    DATATYPE c_z_vol_ratio = corner_volume[c + i] / tile2Ptr[i];
                    tile5Ptr[i] = tile0Ptr[i] * c_z_vol_ratio;
                }
            }

            // Step8: Accumulate zone_gradient
            maa_indirect_rmw_vector<DATATYPE>(zone_gradient.data(), tile3, tile5, Operation_t::ADD_OP, tileCond);
        }
        wait_ready(tile5);
        // #pragma omp for
        // for (int c = 0; c < num_corners; ++c) {
        //     if (corner_type[c] < 1)
        //         continue; // Only operate on interior corners
        //     int z = c_to_z_map[c];
        //     int p = c_to_p_map[c];
        //     double c_z_vol_ratio = corner_volume[c] / zone_volume[z];
        //     zone_gradient[z] += point_gradient[p] * c_z_vol_ratio;
        // }
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
    std::cout << "ROI Ended" << std::endl;
    m5_exit(0);
#endif
}

#ifdef VERIFY
// Verification function
void verify_results(const std::vector<DATATYPE> &original, const std::vector<DATATYPE> &Res, const std::string &vector_name) {
    bool success = true;
    if (original.size() != Res.size()) {
        cout << "Size mismatch in " << vector_name << ": original size " << original.size()
             << ", Res size " << Res.size() << endl;
        success = false;
    } else {
        for (size_t i = 0; i < original.size(); ++i) {
            if (std::fabs(original[i] - Res[i]) > TOLERANCE) {
                cout << "Mismatch in " << vector_name << " at index " << i << ": original " << original[i]
                     << ", Res " << Res[i] << endl;
                success = false;
                break;
            }
        }
    }
    if (success) {
        cout << vector_name << " verification passed." << endl;
    } else {
        cout << vector_name << " verification failed." << endl;
    }
}
#endif

void print_usage(std::string name) {
    cout << "Usage: " << name << " [n]" << endl;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    srand((unsigned)time(NULL));
    int n = stoi(argv[1]);
    float branch_bias = 0.95;

    int total_size = n;
    int num_points = total_size + 2 * PADDING_LEN;
    int num_zones = total_size + 2 * PADDING_LEN;
    int num_corners = total_size;

    corner_type.resize(num_corners);
    corner_volume.resize(num_corners);
    c_to_z_map.resize(num_corners);
    c_to_p_map.resize(num_corners);

    zone_gradient.resize(num_zones);
    point_gradient.resize(num_points);

    zone_volume.resize(num_zones);

    zone_gradient_exp.resize(num_zones);
    zone_volume_exp.resize(num_zones);

    // Initialize data arrays with random values
    for (int i = 0; i < num_corners; ++i) {
        corner_type[i] = (rand() % 100 < branch_bias * 100) ? 1 : -1;
    }
    std::fill(point_gradient.begin(), point_gradient.end(), 1.0);
    std::fill(corner_volume.begin(), corner_volume.end(), 1.0);

    std::fill(zone_volume.begin(), zone_volume.end(), 0.0);
    std::fill(zone_gradient.begin(), zone_gradient.end(), 0.0);

    std::fill(zone_gradient_exp.begin(), zone_gradient_exp.end(), 0.0);
    std::fill(zone_volume_exp.begin(), zone_volume_exp.end(), 0.0);

    // Initialize c_to_p_map and c_to_z_map with random valid indices
    for (int c = DISTANCE_OTEHRS; c < num_corners + DISTANCE_OTEHRS; ++c) {
        int idx = c - DISTANCE_OTEHRS;
        int rand_offset = rand() % (2 * DISTANCE_OTEHRS + 1) - DISTANCE_OTEHRS;
        c_to_p_map[idx] = c + rand_offset;
        if (c_to_p_map[idx] < 0)
            assert(false && "c_to_p_map[c] < 0");
        else if (c_to_p_map[idx] >= num_points)
            assert(false && "c_to_p_map[c] >= num_points");

        rand_offset = rand() % (2 * DISTANCE_OTEHRS + 1) - DISTANCE_OTEHRS;
        c_to_z_map[idx] = c + rand_offset;
        if (c_to_z_map[idx] < 0)
            assert(false && "c_to_z_map[c] < 0");
        else if (c_to_z_map[idx] >= num_zones)
            assert(false && "c_to_z_map[c] >= num_zones");
    }

#ifdef GEM5
    cout << "Starting checkpoint" << endl;
    m5_checkpoint(0, 0);
    cout << "checkpoint done" << endl;
#endif
    alloc_MAA();
    init_MAA();

#ifndef MAA
    // Run original functions
    gradzatz();
#else

#pragma omp parallel
    {
#pragma omp critical
        {
            int thread_id = omp_get_thread_num();
            tiles0[thread_id] = get_new_tile<int>();
            tiles1[thread_id] = get_new_tile<int>();
            tiles2[thread_id] = get_new_tile<int>();
            tiles3[thread_id] = get_new_tile<int>();
            tiles4[thread_id] = get_new_tile<int>();
            regs0[thread_id] = get_new_reg<int>();
            regs1[thread_id] = get_new_reg<int>();
            regs2[thread_id] = get_new_reg<int>();
        }
    }
    gradzatz_MAA();
#ifdef VERIFY
    gradzatz();
    verify_results(zone_gradient_exp, zone_gradient, "zone_gradient");
    verify_results(zone_volume_exp, zone_volume, "zone_volume");
#endif
#endif
#ifdef GEM5
    m5_exit(0);
#endif
    return 0;
}