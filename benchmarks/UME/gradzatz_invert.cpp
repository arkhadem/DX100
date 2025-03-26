#include <iostream>
#include <string>
#include <ctime>
#include <vector>
#include <algorithm> // For std::iota and std::fill
#include <cstdlib>   // For rand()
#include <cmath>     // For std::fabs
#include <omp.h>
using namespace std;

// #define VERIFY

#define DATATYPE float

#define DISTANCE_OTEHRS 85000
#define DISTANCE_P2C    80000
#define PADDING_LEN     90000
#define TOLERANCE       1e-3 // Tolerance for floating-point comparisons

std::vector<int> c_to_z_map;
std::vector<int> c_to_p_map;

// Using CSR format for p_to_c_map
std::vector<int> p_to_c_indptr;  // Size: num_points + 1
std::vector<int> p_to_c_indices; // Flattened indices

// Using CSR format for z_to_c_map
std::vector<int> z_to_c_indptr;  // Size: num_zones + 1
std::vector<int> z_to_c_indices; // Flattened indices

std::vector<DATATYPE> corner_volume;
std::vector<DATATYPE> zone_volume_tmp;
std::vector<DATATYPE> point_normal;
std::vector<DATATYPE> point_gradient;

// outputs
std::vector<DATATYPE> zone_gradient;
std::vector<DATATYPE> zone_gradient_exp;

std::vector<int> zone_type;

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
int tiles0[NUM_CORES], tiles1[NUM_CORES], tiles2[NUM_CORES], tiles3[NUM_CORES], tiles4[NUM_CORES], tilesi[NUM_CORES], tilesj[NUM_CORES];
int regs0[NUM_CORES], regs1[NUM_CORES], regs2[NUM_CORES], regs3[NUM_CORES], regs4[NUM_CORES], last_i_regs[NUM_CORES], last_j_regs[NUM_CORES];

void gradzatz_invert_CSR() {
    int num_zones = zone_gradient_exp.size();
    // Initialize zone_gradient_exp
    zone_gradient_exp.assign(num_zones, 0.0);
#ifdef GEM5
    clear_mem_region();
    // Add memory regions for used arrays in this kernel
    add_mem_region(zone_gradient_exp.data(), zone_gradient_exp.data() + zone_gradient_exp.size()); // 6
    add_mem_region(point_gradient.data(), point_gradient.data() + point_gradient.size());          // 7
    add_mem_region(corner_volume.data(), corner_volume.data() + corner_volume.size());             // 8
    add_mem_region(z_to_c_indices.data(), z_to_c_indices.data() + z_to_c_indices.size());          // 9
    add_mem_region(c_to_p_map.data(), c_to_p_map.data() + c_to_p_map.size());                      // 10
    add_mem_region(z_to_c_indptr.data(), z_to_c_indptr.data() + z_to_c_indptr.size());             // 11
    add_mem_region(zone_type.data(), zone_type.data() + zone_type.size());                         // 12
    std::cout << "ROI Begin" << std::endl;
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
#pragma omp parallel
    {
#pragma omp for
        // For each zone, compute the gradient using CSR format
        for (int z = 0; z < num_zones; ++z) {
            if (zone_type[z] < 1)
                continue; // Only operate on local interior zones

            // Accumulate the local zone volume
            double zone_vol = 0.0;
            for (int idx = z_to_c_indptr[z]; idx < z_to_c_indptr[z + 1]; ++idx) {
                int c = z_to_c_indices[idx];
                zone_vol += corner_volume[c];
            }

            // Accumulate the zone-centered gradient
            for (int idx = z_to_c_indptr[z]; idx < z_to_c_indptr[z + 1]; ++idx) {
                int c = z_to_c_indices[idx];
                int p = c_to_p_map[c];
                double c_z_vol_ratio = corner_volume[c] / zone_vol;
#ifdef VERIFY
#pragma omp atomic
                zone_gradient_exp[z] += point_gradient[p] * c_z_vol_ratio;
#else
                zone_gradient_exp[z] += point_gradient[p] * c_z_vol_ratio;
#endif
            }
        }
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
    std::cout << "ROI Ended" << std::endl;
    m5_exit(0);
#endif
}

void gradzatz_invert_MAA_CSR() {
    int num_zones = zone_gradient.size();

#ifdef GEM5
    clear_mem_region();
    // Add memory regions for used arrays in this kernel
    add_mem_region(zone_gradient.data(), zone_gradient.data() + zone_gradient.size());       // 6
    add_mem_region(point_gradient.data(), point_gradient.data() + point_gradient.size());    // 7
    add_mem_region(corner_volume.data(), corner_volume.data() + corner_volume.size());       // 8
    add_mem_region(z_to_c_indices.data(), z_to_c_indices.data() + z_to_c_indices.size());    // 9
    add_mem_region(c_to_p_map.data(), c_to_p_map.data() + c_to_p_map.size());                // 10
    add_mem_region(z_to_c_indptr.data(), z_to_c_indptr.data() + z_to_c_indptr.size());       // 11
    add_mem_region(zone_type.data(), zone_type.data() + zone_type.size());                   // 12
    add_mem_region(zone_volume_tmp.data(), zone_volume_tmp.data() + zone_volume_tmp.size()); // 13
    std::cout << "ROI Begin" << std::endl;
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
// For each zone, compute the gradient using CSR format
#pragma omp parallel
    {
        int reg0, reg1, reg2, reg3, reg4, last_i_reg, last_j_reg;
        int tile0, tilelb, tileub, tile3, tile5, tilei, tilej;
        int thread_id = omp_get_thread_num();
        tile5 = tiles0[thread_id];
        tilei = tiles1[thread_id];
        tilej = tiles2[thread_id];
        tile0 = tiles3[thread_id];
        tilelb = tiles4[thread_id];
        tileub = tilesi[thread_id];
        tile3 = tilesj[thread_id];
        reg0 = regs0[thread_id];
        reg1 = regs1[thread_id];
        reg2 = regs2[thread_id];
        reg3 = regs3[thread_id];
        reg4 = regs4[thread_id];
        last_i_reg = last_i_regs[thread_id];
        last_j_reg = last_j_regs[thread_id];
        maa_const<int>(num_zones, reg1);
        maa_const<int>(1, reg2);
        int *tilei_ptr = get_cacheable_tile_pointer<int>(tilei);
        DATATYPE *tile5_ptr = get_cacheable_tile_pointer<DATATYPE>(tile5);
        DATATYPE *tilej_ptr = get_cacheable_tile_pointer<DATATYPE>(tilej);
#pragma omp for
        for (int zidx = 0; zidx < num_zones; zidx += TILE_SIZE) {
            maa_const<int>(zidx, reg0);
            // load lb and ub of z_to_c_indptr
            maa_stream_load<int>(z_to_c_indptr.data(), reg0, reg1, reg2, tilelb);
            maa_stream_load<int>(z_to_c_indptr.data() + 1, reg0, reg1, reg2, tileub);
            int curr_tilej_size = 0;
            DATATYPE *zone_vol = zone_volume_tmp.data() + zidx;
            maa_const<int>(0, last_i_reg);
            maa_const<int>(-1, last_j_reg);
            int idx_max = z_to_c_indptr[min(zidx + TILE_SIZE, num_zones)];
            int *curr_zone_type = zone_type.data() + zidx;
            float *curr_zone_gradient = zone_gradient.data() + zidx;
            maa_const(idx_max, reg3);
            for (int idx_base = z_to_c_indptr[zidx]; idx_base < idx_max; idx_base += TILE_SIZE) {
                maa_const(idx_base, reg4);
                maa_range_loop<int>(last_i_reg, last_j_reg, tilelb, tileub, reg2, tilei, tilej);
                // Step2: load c_to_z_map
                maa_stream_load<int>(z_to_c_indices.data(), reg4, reg3, reg2, tile0); // 0->c
                // Step3: load corner_volume[c]
                maa_indirect_load<DATATYPE>(corner_volume.data(), tile0, tile5);
                // Step4: accumulate zone_vol
                // Transfer tile4 assume range is after this line
                maa_indirect_rmw_vector<DATATYPE>(zone_vol, tilei, tile5, Operation_t::ADD_OP);
                wait_ready(tile0);
            }
            wait_ready(tile5);
            // double zone_vol = 0.0;
            // for (int idx = z_to_c_indptr[z]; idx < z_to_c_indptr[z + 1]; ++idx) {
            //     int c = z_to_c_indices[idx];
            //     zone_vol += corner_volume[c];
            // }

            maa_const<int>(0, last_i_reg);
            maa_const<int>(-1, last_j_reg);
            for (int idx_base = z_to_c_indptr[zidx]; idx_base < idx_max; idx_base += TILE_SIZE) {
                maa_const(idx_base, reg4);
                maa_range_loop<int>(last_i_reg, last_j_reg, tilelb, tileub, reg2, tilei, tilej);
                // Step2: load z_to_c_indices
                maa_stream_load<int>(z_to_c_indices.data(), reg4, reg3, reg2, tile0); // 0->c
                // Step3: load corner_volume[c]
                maa_indirect_load<DATATYPE>(corner_volume.data(), tile0, tile5); // 5->corner_volume
                // Transfer tile0 and tilej
                // Step4: load c_to_p_map
                maa_indirect_load<int>(c_to_p_map.data(), tile0, tile3); // 3->p
                // Step5: load point_gradient
                maa_indirect_load<DATATYPE>(point_gradient.data(), tile3, tilej); // 0->point_gradient
                curr_tilej_size = min(idx_max - idx_base, TILE_SIZE);
                wait_ready(tile0);
                wait_ready(tilej);
#pragma omp simd simdlen(4)
                for (int j = 0; j < curr_tilej_size; j++) {
                    if (curr_zone_type[tilei_ptr[j]] < 1)
                        continue; // Only operate on local interior zones
                    int corner_volume = tile5_ptr[j];
                    int point_gradient = tilej_ptr[j];
                    DATATYPE c_z_vol_ratio = corner_volume / zone_vol[tilei_ptr[j]];
                    curr_zone_gradient[tilei_ptr[j]] += point_gradient * c_z_vol_ratio;
                }
            }
        }
        // for (int z = 0; z < num_zones; ++z) {
        //     if (zone_type[z] < 1)
        //         continue; // Only operate on local interior zones
        //             for (int idx = z_to_c_indptr[z]; idx < z_to_c_indptr[z + 1]; ++idx) {
        //                 int c = z_to_c_indices[idx];
        //                 int p = c_to_p_map[c];
        //                 double c_z_vol_ratio = corner_volume[c] / zone_vol;
        // #pragma omp atomic
        //                 zone_gradient_exp[z] += point_gradient[p] * c_z_vol_ratio;
        //             }
    } // parallel
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

void init_inverse_map_CSR(int num_points, int num_zones, int num_corners) {
    // Build CSR format for p_to_c_map
    p_to_c_indptr.resize(num_points + 1, 0);
    std::vector<int> p_to_c_count(num_points, 0); // Temporary count array

    // First pass: count the number of entries per point
    for (int c = 0; c < num_corners; ++c) {
        int p = c_to_p_map[c];
        p_to_c_count[p]++;
    }

    // Build the indptr array
    p_to_c_indptr[0] = 0;
    for (int p = 0; p < num_points; ++p) {
        p_to_c_indptr[p + 1] = p_to_c_indptr[p] + p_to_c_count[p];
    }

    // Allocate indices array
    p_to_c_indices.resize(p_to_c_indptr[num_points]);

    // Reset count array to reuse it
    std::fill(p_to_c_count.begin(), p_to_c_count.end(), 0);

    // Second pass: fill the indices array
    for (int c = 0; c < num_corners; ++c) {
        int p = c_to_p_map[c];
        int idx = p_to_c_indptr[p] + p_to_c_count[p];
        p_to_c_indices[idx] = c;
        p_to_c_count[p]++;
    }

    // Build CSR format for z_to_c_map
    z_to_c_indptr.resize(num_zones + 1, 0);
    std::vector<int> z_to_c_count(num_zones, 0); // Temporary count array

    // First pass: count the number of entries per zone
    for (int c = 0; c < num_corners; ++c) {
        int z = c_to_z_map[c];
        z_to_c_count[z]++;
    }

    // Build the indptr array
    z_to_c_indptr[0] = 0;
    for (int z = 0; z < num_zones; ++z) {
        z_to_c_indptr[z + 1] = z_to_c_indptr[z] + z_to_c_count[z];
    }

    // Allocate indices array
    z_to_c_indices.resize(z_to_c_indptr[num_zones]);

    // Reset count array to reuse it
    z_to_c_count.assign(num_zones, 0);

    // Second pass: fill the indices array
    for (int c = 0; c < num_corners; ++c) {
        int z = c_to_z_map[c];
        int idx = z_to_c_indptr[z] + z_to_c_count[z];
        z_to_c_indices[idx] = c;
        z_to_c_count[z]++;
    }
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

    corner_volume.resize(num_corners);
    c_to_z_map.resize(num_corners);
    c_to_p_map.resize(num_corners);

    zone_gradient.resize(num_zones);
    point_gradient.resize(num_points);

    zone_volume_tmp.resize(num_zones);
    point_normal.resize(num_points);
    zone_type.resize(num_zones);

    zone_gradient_exp.resize(num_zones);

    std::fill(zone_type.begin(), zone_type.end(), 1);

    // Initialize data arrays with random values
    for (int i = 0; i < num_points; ++i) {
        zone_type[i] = (rand() % 100 < branch_bias * 100) ? 1 : -1;
    }
    std::fill(point_gradient.begin(), point_gradient.end(), 1.0);
    std::fill(corner_volume.begin(), corner_volume.end(), 1.0);
    std::fill(point_normal.begin(), point_normal.end(), 1.0);
    std::fill(zone_volume_tmp.begin(), zone_volume_tmp.end(), 0.0);

    std::fill(zone_gradient.begin(), zone_gradient.end(), 0.0);
    std::fill(zone_gradient_exp.begin(), zone_gradient_exp.end(), 0.0);

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
        ;
    }

    init_inverse_map_CSR(num_points, num_zones, num_corners);

#ifdef GEM5
    cout << "Starting checkpoint" << endl;
    m5_checkpoint(0, 0);
    cout << "checkpoint done" << endl;
#endif
    alloc_MAA();
    init_MAA();

#ifndef MAA
    // Run original functions
    gradzatz_invert_CSR();
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
            tilesi[thread_id] = get_new_tile<int>();
            tilesj[thread_id] = get_new_tile<int>();
            regs0[thread_id] = get_new_reg<int>();
            regs1[thread_id] = get_new_reg<int>();
            regs2[thread_id] = get_new_reg<int>();
            regs3[thread_id] = get_new_reg<int>();
            regs4[thread_id] = get_new_reg<int>();
            last_i_regs[thread_id] = get_new_reg<int>();
            last_j_regs[thread_id] = get_new_reg<int>();
        }
    }
    gradzatz_invert_MAA_CSR();
#ifdef VERIFY
    gradzatz_invert_CSR();
    verify_results(zone_gradient_exp, zone_gradient, "zone_gradient");
#endif
#endif
#ifdef GEM5
    m5_exit(0);
#endif
    return 0;
}