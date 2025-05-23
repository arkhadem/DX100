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

std::vector<int> corner_type;
std::vector<int> c_to_z_map;
std::vector<int> c_to_p_map;
std::vector<DATATYPE> point_volume;
std::vector<DATATYPE> point_gradient;
std::vector<DATATYPE> corner_volume;
std::vector<DATATYPE> csurf;

std::vector<DATATYPE> zone_field;
std::vector<DATATYPE> point_normal;

std::vector<DATATYPE> point_volume_exp;
std::vector<DATATYPE> point_gradient_exp;

std::vector<int> point_type;
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
int tiles0[NUM_CORES], tiles1[NUM_CORES], tiles2[NUM_CORES], tiles3[NUM_CORES], tiles4[NUM_CORES], tiles5[NUM_CORES];
int regs0[NUM_CORES], regs1[NUM_CORES], regs2[NUM_CORES];

void gradzatp() {
    int pll = point_volume_exp.size();
    int cl = corner_type.size();

#ifdef GEM5
    clear_mem_region();
    // Add memory regions for used arrays in this kernel
    add_mem_region(point_volume_exp.data(), point_volume_exp.data() + point_volume_exp.size());       // 6
    add_mem_region(point_gradient_exp.data(), point_gradient_exp.data() + point_gradient_exp.size()); // 7
    add_mem_region(corner_volume.data(), corner_volume.data() + corner_volume.size());                // 8
    add_mem_region(csurf.data(), csurf.data() + csurf.size());                                        // 9
    add_mem_region(zone_field.data(), zone_field.data() + zone_field.size());                         // 10
    add_mem_region(c_to_p_map.data(), c_to_p_map.data() + c_to_p_map.size());                         // 11
    add_mem_region(c_to_z_map.data(), c_to_z_map.data() + c_to_z_map.size());                         // 12
    add_mem_region(point_type.data(), point_type.data() + point_type.size());                         // 13
    add_mem_region(corner_type.data(), corner_type.data() + corner_type.size());                      // 14
    add_mem_region(point_normal.data(), point_normal.data() + point_normal.size());                   // 15
    std::cout << "ROI Begin" << std::endl;
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
#pragma omp parallel
    {
#pragma omp for
        for (int c = 0; c < cl; ++c) {
            if (corner_type[c] < 1)
                continue; // Only operate on interior corners
            int const z = c_to_z_map[c];
            int const p = c_to_p_map[c];
#pragma omp atomic
            point_volume_exp[p] += corner_volume[c];
#pragma omp atomic
            point_gradient_exp[p] += csurf[c] * zone_field[z];
        }
        /*
        Divide by point control volume to get gradient. If a point is on the outer
        perimeter of the mesh (POINT_TYPE=-1), subtract the outward normal component
        of the gradient using the point normals.
        */
#pragma omp for
        for (int p = 0; p < pll; ++p) {
            if (point_type[p] > 0) {
                // Internal point
                point_gradient_exp[p] /= point_volume_exp[p];
            } else if (point_type[p] == -1) {
                double const ppdot = point_gradient_exp[p] * point_normal[p];
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

void gradzatp_MAA() {
    int pll = point_volume.size();
    int cl = corner_type.size();

#ifdef GEM5
    clear_mem_region();
    // Add memory regions for used arrays in this kernel
    add_mem_region(point_volume.data(), point_volume.data() + point_volume.size());       // 6
    add_mem_region(point_gradient.data(), point_gradient.data() + point_gradient.size()); // 7
    add_mem_region(corner_volume.data(), corner_volume.data() + corner_volume.size());    // 8
    add_mem_region(csurf.data(), csurf.data() + csurf.size());                            // 9
    add_mem_region(zone_field.data(), zone_field.data() + zone_field.size());             // 10
    add_mem_region(c_to_p_map.data(), c_to_p_map.data() + c_to_p_map.size());             // 11
    add_mem_region(c_to_z_map.data(), c_to_z_map.data() + c_to_z_map.size());             // 12
    add_mem_region(point_type.data(), point_type.data() + point_type.size());             // 13
    add_mem_region(corner_type.data(), corner_type.data() + corner_type.size());          // 14
    add_mem_region(point_normal.data(), point_normal.data() + point_normal.size());       // 15
    std::cout << "ROI Begin" << std::endl;
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
#pragma omp parallel
    {
        int reg0, reg1, reg2;
        int tile0, tile1, tile2, tile3, tile4, tileCond;
        int omp_thread_id = omp_get_thread_num();
        reg0 = regs0[omp_thread_id];
        reg1 = regs1[omp_thread_id];
        reg2 = regs2[omp_thread_id];
        tile0 = tiles0[omp_thread_id];
        tile1 = tiles1[omp_thread_id];
        tile2 = tiles2[omp_thread_id];
        tileCond = tiles4[omp_thread_id];
        tile3 = tiles3[omp_thread_id];
        tile4 = tiles5[omp_thread_id];
        maa_const<int>(1, reg2);
        maa_const<int>(cl, reg1);
// int* tile_cond_ptr = get_cacheable_tile_pointer<int>(tileCond);
#pragma omp for
        for (int c = 0; c < cl; c += TILE_SIZE) {
            maa_const<int>(c, reg0);
            // Step1: Load corner_type
            maa_stream_load<int>(corner_type.data(), reg0, reg1, reg2, tile0);
            // Step2: Perform comparison
            maa_alu_scalar<int>(tile0, reg2, tileCond, Operation_t::GTE_OP);
            // for (int i = 0; i < TILE_SIZE; i++){
            //     cout << "tileCond[" << i << "]: " << tile_cond_ptr[i] << endl;
            // }
            // Step3: Load c_to_z_map and c_to_p_map
            maa_stream_load<int>(c_to_z_map.data(), reg0, reg1, reg2, tile3, tileCond);
            maa_stream_load<int>(c_to_p_map.data(), reg0, reg1, reg2, tile4, tileCond);

            // Step4: Load corner_volume[c], zone_field[z], and csurf[c]
            maa_stream_load<DATATYPE>(corner_volume.data(), reg0, reg1, reg2, tile0, tileCond);
            maa_indirect_rmw_vector<DATATYPE>(point_volume.data(), tile4, tile0, Operation_t::ADD_OP, tileCond);
            // transfer tile4, tileCond, tile3
            maa_indirect_load<DATATYPE>(zone_field.data(), tile3, tile0, tileCond);
            maa_stream_load<DATATYPE>(csurf.data(), reg0, reg1, reg2, tile1, tileCond);
            // DO ALU operation
            maa_alu_vector<DATATYPE>(tile1, tile0, tile2, Operation_t::MUL_OP, tileCond);
            // rmw to point_gradient
            maa_indirect_rmw_vector<DATATYPE>(point_gradient.data(), tile4, tile2, Operation_t::ADD_OP, tileCond);
            wait_ready(tile1);
            // if (corner_type[c] < 1)
            //     continue; // Only operate on interior corners
            // int const z = c_to_z_map[c];
            // int const p = c_to_p_map[c];
            // point_volume[p] += corner_volume[c];
            // point_gradient[p] += csurf[c] * zone_field[z];
        }
        wait_ready(tile2);
/*
        Divide by point control volume to get gradient. If a point is on the outer
        perimeter of the mesh (POINT_TYPE=-1), subtract the outward normal component
        of the gradient using the point normals.
        */
#pragma omp for
        for (int p = 0; p < pll; ++p) {
            if (point_type[p] > 0) {
                // Internal point
                point_gradient[p] /= point_volume[p];
            } else if (point_type[p] == -1) {
                double const ppdot = point_gradient[p] * point_normal[p];
                point_gradient[p] = (point_gradient[p] - point_normal[p] * ppdot) / point_volume[p];
            }
        } // for
    } // omp parallel
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
    csurf.resize(num_corners);
    corner_volume.resize(num_corners);
    c_to_z_map.resize(num_corners);
    c_to_p_map.resize(num_corners);

    point_volume.resize(num_points);
    point_gradient.resize(num_points);

    zone_field.resize(num_zones);
    point_normal.resize(num_points);
    point_type.resize(num_points);
    zone_type.resize(num_zones);

    point_volume_exp.resize(num_points);
    point_gradient_exp.resize(num_points);

    // Initialize point_type and zone_type (assuming all internal for simplicity)
    std::fill(point_type.begin(), point_type.end(), 1);
    std::fill(zone_type.begin(), zone_type.end(), 1);

    // Initialize data arrays with random values
    for (int i = 0; i < num_corners; ++i) {
        corner_type[i] = (rand() % 100 < branch_bias * 100) ? 1 : -1;
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

#ifdef GEM5
    cout << "Starting checkpoint" << endl;
    m5_checkpoint(0, 0);
    cout << "checkpoint done" << endl;
#endif
    alloc_MAA();
    init_MAA();

#ifndef MAA
    // Run original functions
    gradzatp();
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
            regs0[thread_id] = get_new_reg<int>();
            regs1[thread_id] = get_new_reg<int>();
            regs2[thread_id] = get_new_reg<int>();
        }
    }
    gradzatp_MAA();
#ifdef VERIFY
    gradzatp();
    verify_results(point_volume_exp, point_volume, "point_volume");
    verify_results(point_gradient_exp, point_gradient, "point_gradient");
#endif
#endif
#ifdef GEM5
    m5_exit(0);
#endif
    return 0;
}