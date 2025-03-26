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
std::vector<DATATYPE> csurf;
std::vector<DATATYPE> zone_field;
std::vector<DATATYPE> point_normal;

std::vector<DATATYPE> point_volume;
std::vector<DATATYPE> point_gradient;
std::vector<DATATYPE> point_volume_exp;
std::vector<DATATYPE> point_gradient_exp;

std::vector<int> point_type;

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
int tiles0[NUM_CORES], tiles1[NUM_CORES], tiles2[NUM_CORES], tiles3[NUM_CORES], tiles4[NUM_CORES], tiles5[NUM_CORES], tilesi[NUM_CORES];
int regs0[NUM_CORES], regs1[NUM_CORES], regs2[NUM_CORES], regs3[NUM_CORES], regs4[NUM_CORES], last_i_regs[NUM_CORES], last_j_regs[NUM_CORES];

void gradzatp_invert_CSR() {
    int num_points = point_volume_exp.size();

    // Initialize point_volume_exp and point_gradient
    point_volume_exp.assign(num_points, 0.0);
    point_gradient_exp.assign(num_points, 0.0);
#ifdef GEM5
    clear_mem_region();
    // Add memory regions for used arrays in this kernel
    add_mem_region(point_volume_exp.data(), point_volume_exp.data() + point_volume_exp.size());       // 6
    add_mem_region(point_gradient_exp.data(), point_gradient_exp.data() + point_gradient_exp.size()); // 7
    add_mem_region(corner_volume.data(), corner_volume.data() + corner_volume.size());                // 8
    add_mem_region(csurf.data(), csurf.data() + csurf.size());                                        // 9
    add_mem_region(zone_field.data(), zone_field.data() + zone_field.size());                         // 10
    add_mem_region(p_to_c_indices.data(), p_to_c_indices.data() + p_to_c_indices.size());             // 11
    add_mem_region(c_to_z_map.data(), c_to_z_map.data() + c_to_z_map.size());                         // 12
    add_mem_region(p_to_c_indptr.data(), p_to_c_indptr.data() + p_to_c_indptr.size());                // 13
    add_mem_region(point_type.data(), point_type.data() + point_type.size());                         // 14
    add_mem_region(point_normal.data(), point_normal.data() + point_normal.size());                   // 15
    std::cout << "ROI Begin" << std::endl;
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
#pragma omp parallel
    {
#pragma omp for
        // For each point, iterate over its associated corners using CSR format
        for (int p = 0; p < num_points; ++p) {
            for (int idx = p_to_c_indptr[p]; idx < p_to_c_indptr[p + 1]; ++idx) {
                int c = p_to_c_indices[idx];
                int z = c_to_z_map[c];
#ifdef VERIFY
#pragma omp atomic
                point_volume_exp[p] += corner_volume[c];
#pragma omp atomic
                point_gradient_exp[p] += csurf[c] * zone_field[z];
#else
                point_volume_exp[p] += corner_volume[c];
                point_gradient_exp[p] += csurf[c] * zone_field[z];
#endif
            }
        }

#pragma omp for
        // Divide by point control volume to get gradient
        for (int p = 0; p < num_points; ++p) {
            if (point_type[p] > 0) {
                // Internal point
                point_gradient_exp[p] /= point_volume_exp[p];
            } else if (point_type[p] == -1) {
                double ppdot = point_gradient_exp[p] * point_normal[p];
                point_gradient_exp[p] = (point_gradient_exp[p] - point_normal[p] * ppdot) / point_volume_exp[p];
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

void gradzatp_invert_MAA_CSR() {
    int num_points = point_volume.size();

#ifdef GEM5
    clear_mem_region();
    // Add memory regions for used arrays in this kernel
    add_mem_region(point_volume.data(), point_volume.data() + point_volume.size());       // 6
    add_mem_region(point_gradient.data(), point_gradient.data() + point_gradient.size()); // 7
    add_mem_region(corner_volume.data(), corner_volume.data() + corner_volume.size());    // 8
    add_mem_region(csurf.data(), csurf.data() + csurf.size());                            // 9
    add_mem_region(zone_field.data(), zone_field.data() + zone_field.size());             // 10
    add_mem_region(p_to_c_indices.data(), p_to_c_indices.data() + p_to_c_indices.size()); // 11
    add_mem_region(c_to_z_map.data(), c_to_z_map.data() + c_to_z_map.size());             // 12
    add_mem_region(p_to_c_indptr.data(), p_to_c_indptr.data() + p_to_c_indptr.size());    // 13
    add_mem_region(point_type.data(), point_type.data() + point_type.size());             // 14
    add_mem_region(point_normal.data(), point_normal.data() + point_normal.size());       // 15
    std::cout << "ROI Begin" << std::endl;
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
#pragma omp parallel
    {
        int reg0, reg1, reg2, reg3, reg4, last_i_reg, last_j_reg;
        int tile0, tilelb, tileub, tile3, tile5, tilei, tilej;
        int thread_id = omp_get_thread_num();
        tile0 = tiles0[thread_id];
        tile5 = tiles1[thread_id];
        tilei = tiles2[thread_id];
        tilej = tiles3[thread_id];
        tilelb = tiles4[thread_id];
        tileub = tiles5[thread_id];
        tile3 = tilesi[thread_id];
        reg0 = regs0[thread_id];
        reg1 = regs1[thread_id];
        reg2 = regs2[thread_id];
        reg3 = regs3[thread_id];
        reg4 = regs4[thread_id];
        last_i_reg = last_i_regs[thread_id];
        last_j_reg = last_j_regs[thread_id];
        maa_const<int>(num_points, reg1);
        maa_const<int>(1, reg2);

        // for (int p = 0; p < num_points; ++p) {
        //     for (int j = p_to_c_indptr[p]; j < p_to_c_indptr[p + 1]; ++j) {
        //         int c = p_to_c_indices[j];
        //         int z = c_to_z_map[c];
        //         point_volume[p] += corner_volume[c];
        //         point_gradient[p] += csurf[c] * zone_field[z];
        //     }
        // }
        DATATYPE *tilej_ptr = get_cacheable_tile_pointer<DATATYPE>(tilej);
        DATATYPE *tile5_ptr = get_cacheable_tile_pointer<DATATYPE>(tile5);
        DATATYPE *tile0_ptr = get_cacheable_tile_pointer<DATATYPE>(tile0);
        int *tilei_ptr = get_cacheable_tile_pointer<int>(tilei);
#pragma omp for
        for (int p = 0; p < num_points; p += TILE_SIZE) {
            maa_const<int>(p, reg0);
            // Step1: stream load lb and ub of p_to_c_indptr
            maa_stream_load<int>(p_to_c_indptr.data(), reg0, reg1, reg2, tilelb);
            maa_stream_load<int>(p_to_c_indptr.data() + 1, reg0, reg1, reg2, tileub);
            int curr_tilej_size = 0;
            maa_const<int>(0, last_i_reg);
            maa_const<int>(-1, last_j_reg);
            int j_max = p_to_c_indptr[min(p + TILE_SIZE, num_points)];
            DATATYPE *curr_point_volume = point_volume.data() + p;
            DATATYPE *curr_point_gradient = point_gradient.data() + p;
            maa_const(j_max, reg3);
            for (int j_base = p_to_c_indptr[p]; j_base < j_max; j_base += TILE_SIZE) {
                maa_const(j_base, reg4);
                maa_range_loop<int>(last_i_reg, last_j_reg, tilelb, tileub, reg2, tilei, tilej);
                // Step2: load c using p_to_c_indices and tilej
                maa_stream_load<int>(p_to_c_indices.data(), reg4, reg3, reg2, tile0); // 0->c
                // Step3: load c_to_z_map
                maa_indirect_load<int>(c_to_z_map.data(), tile0, tile3); // 3->z
                // Step4: load corner_volume[c]
                maa_indirect_load<DATATYPE>(corner_volume.data(), tile0, tilej); // j->corner_volume
                // Transfer tile0 (ssuming step 3 is also here)
                // Step6: load csurf[c]
                maa_indirect_load<DATATYPE>(csurf.data(), tile0, tile5); // 5->csurf
                // Step7: load zone_field[z]
                maa_indirect_load<DATATYPE>(zone_field.data(), tile3, tile0); // 0->zone_field
                curr_tilej_size = min(j_max - j_base, TILE_SIZE);
                wait_ready(tilei);
                wait_ready(tile0);
#pragma omp simd simdlen(4)
                for (int j = 0; j < curr_tilej_size; j++) {
                    curr_point_volume[tilei_ptr[j]] += tilej_ptr[j];
                    curr_point_gradient[tilei_ptr[j]] += tile5_ptr[j] * tile0_ptr[j];
                }
            }
        }

// Divide by point control volume to get gradient
#pragma omp for
        for (int p = 0; p < num_points; ++p) {
            if (point_type[p] > 0) {
                // Internal point
                point_gradient[p] /= point_volume[p];
            } else if (point_type[p] == -1) {
                double ppdot = point_gradient[p] * point_normal[p];
                point_gradient[p] = (point_gradient[p] - point_normal[p] * ppdot) / point_volume[p];
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

    csurf.resize(num_corners);
    corner_volume.resize(num_corners);
    c_to_z_map.resize(num_corners);
    c_to_p_map.resize(num_corners);

    point_volume.resize(num_points);
    point_gradient.resize(num_points);

    zone_field.resize(num_zones);
    point_normal.resize(num_points);
    point_type.resize(num_points);

    point_volume_exp.resize(num_points);
    point_gradient_exp.resize(num_points);

    std::fill(point_type.begin(), point_type.end(), 1);

    // Initialize data arrays with random values
    for (int i = 0; i < num_points; ++i) {
        point_type[i] = (rand() % 100 < branch_bias * 100) ? 1 : -1;
    }
    std::fill(corner_volume.begin(), corner_volume.end(), 1.0);
    std::fill(point_normal.begin(), point_normal.end(), 1.0);
    std::fill(csurf.begin(), csurf.end(), 1.0);
    std::fill(zone_field.begin(), zone_field.end(), 1.0);

    std::fill(point_volume.begin(), point_volume.end(), 0.0);
    std::fill(point_gradient.begin(), point_gradient.end(), 0.0);
    std::fill(point_volume_exp.begin(), point_volume_exp.end(), 0.0);
    std::fill(point_gradient_exp.begin(), point_gradient_exp.end(), 0.0);

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
    gradzatp_invert_CSR();
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
            tiles5[thread_id] = get_new_tile<int>();
            tilesi[thread_id] = get_new_tile<int>();
            regs0[thread_id] = get_new_reg<int>();
            regs1[thread_id] = get_new_reg<int>();
            regs2[thread_id] = get_new_reg<int>();
            regs3[thread_id] = get_new_reg<int>();
            regs4[thread_id] = get_new_reg<int>();
            last_i_regs[thread_id] = get_new_reg<int>();
            last_j_regs[thread_id] = get_new_reg<int>();
        }
    }
    gradzatp_invert_MAA_CSR();
#ifdef VERIFY
    gradzatp_invert_CSR();
    verify_results(point_volume_exp, point_volume, "point_volume");
    verify_results(point_gradient_exp, point_gradient, "point_gradient");
#endif
#endif
#ifdef GEM5
    m5_exit(0);
#endif
    return 0;
}