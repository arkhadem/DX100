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

#define DISTANCE_OTEHRS 85000
#define DISTANCE_P2C 80000
#define PADDING_LEN 90000
#define TOLERANCE 1e-3 // Tolerance for floating-point comparisons

std::vector<int> corner_type;
std::vector<int> c_to_z_map;
std::vector<int> c_to_p_map;
std::vector<std::vector<int>> z_to_c_map; // Changed to vector of vectors
std::vector<std::vector<int>> p_to_c_map; // Changed to vector of vectors

// Using CSR format for p_to_c_map
std::vector<int> p_to_c_indptr;  // Size: num_points + 1
std::vector<int> p_to_c_indices; // Flattened indices

// Using CSR format for z_to_c_map
std::vector<int> z_to_c_indptr;  // Size: num_zones + 1
std::vector<int> z_to_c_indices; // Flattened indices

std::vector<DATATYPE> point_volume;
std::vector<DATATYPE> point_gradient;
std::vector<DATATYPE> corner_volume;
std::vector<DATATYPE> csurf;
std::vector<DATATYPE> zone_field;
std::vector<DATATYPE> zone_volume;
std::vector<DATATYPE> zone_gradient;
std::vector<DATATYPE> point_normal;

std::vector<DATATYPE> point_volume_exp;
std::vector<DATATYPE> point_gradient_exp;
std::vector<DATATYPE> zone_gradient_exp;
std::vector<DATATYPE> zone_volume_exp;

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
#define NUM_THREADS 4
int tiles0[NUM_THREADS], tiles1[NUM_THREADS], tiles2[NUM_THREADS], tiles3[NUM_THREADS], tiles4[NUM_THREADS], tiles5[NUM_THREADS], tilesi[NUM_THREADS], tilesj[NUM_THREADS];
int regs0[NUM_THREADS], regs1[NUM_THREADS], regs2[NUM_THREADS], regs3[NUM_THREADS], regs4[NUM_THREADS], regs5[NUM_THREADS], last_i_regs[NUM_THREADS], last_j_regs[NUM_THREADS];

void gradzatp_invert() {
    int num_points = point_volume_exp.size();
    // Initialize point_volume_exp and point_gradient_exp
    point_volume_exp.assign(num_points, 0.0);
    point_gradient_exp.assign(num_points, 0.0);
    // For each point, iterate over its associated corners
    for (int p = 0; p < num_points; ++p) {
        for (int c : p_to_c_map[p]) {
            int z = c_to_z_map[c];
            point_volume_exp[p] += corner_volume[c];
            point_gradient_exp[p] += csurf[c] * zone_field[z];
        }
    }
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

void gradzatz_invert() {
    // Get the field gradient at each mesh point
    gradzatp_invert();
    int num_zones = zone_gradient_exp.size();
    // Initialize zone_gradient_exp
    zone_gradient_exp.assign(num_zones, 0.0);
    // For each zone, compute the gradient
    for (int z = 0; z < num_zones; ++z) {
        if (zone_type[z] < 1)
            continue; // Only operate on local interior zones
        // Accumulate the local zone volume
        double zone_vol = 0.0;
        for (int c : z_to_c_map[z]) {
            zone_vol += corner_volume[c];
        }
        // Accumulate the zone-centered gradient
        for (int c : z_to_c_map[z]) {
            int p = c_to_p_map[c];
            double c_z_vol_ratio = corner_volume[c] / zone_vol;
            zone_gradient_exp[z] += point_gradient_exp[p] * c_z_vol_ratio;
        }
    }
}


void gradzatp_invert_CSR() {
    int num_points = point_volume.size();

    // Initialize point_volume and point_gradient
    point_volume.assign(num_points, 0.0);
    point_gradient.assign(num_points, 0.0);

    // For each point, iterate over its associated corners using CSR format
    for (int p = 0; p < num_points; ++p) {
        for (int idx = p_to_c_indptr[p]; idx < p_to_c_indptr[p + 1]; ++idx) {
            int c = p_to_c_indices[idx];
            int z = c_to_z_map[c];
            point_volume[p] += corner_volume[c];
            point_gradient[p] += csurf[c] * zone_field[z];
        }
    }

    // Divide by point control volume to get gradient
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

void gradzatz_invert_CSR() {
    // Get the field gradient at each mesh point
    gradzatp_invert_CSR();

    int num_zones = zone_gradient.size();

    // Initialize zone_gradient
    zone_gradient.assign(num_zones, 0.0);

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
            zone_gradient[z] += point_gradient[p] * c_z_vol_ratio;
        }
    }
}

void gradzatp_invert_MAA_CSR() {
    int num_points = point_volume.size();
    // Initialize point_volume and point_gradient
    point_volume.assign(num_points, 0.0);
    point_gradient.assign(num_points, 0.0);
    // For each point, iterate over its associated corners
    for (int p = 0; p < num_points; ++p) {
        for (int c : p_to_c_map[p]) {
            int z = c_to_z_map[c];
            point_volume[p] += corner_volume[c];
            point_gradient[p] += csurf[c] * zone_field[z];
        }
    }
    // Divide by point control volume to get gradient
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

void gradzatz_invert_MAA_CSR() {
    // Get the field gradient at each mesh point
    gradzatp_invert_MAA_CSR();
    int num_zones = zone_gradient.size();
    // Initialize zone_gradient
    zone_gradient.assign(num_zones, 0.0);
    // For each zone, compute the gradient
    for (int z = 0; z < num_zones; ++z) {
        if (zone_type[z] < 1)
            continue; // Only operate on local interior zones
        // Accumulate the local zone volume
        double zone_vol = 0.0;
        for (int c : z_to_c_map[z]) {
            zone_vol += corner_volume[c];
        }
        // Accumulate the zone-centered gradient
        for (int c : z_to_c_map[z]) {
            int p = c_to_p_map[c];
            double c_z_vol_ratio = corner_volume[c] / zone_vol;
            zone_gradient[z] += point_gradient[p] * c_z_vol_ratio;
        }
    }
}


// Verification function
void verify_results(const std::vector<DATATYPE>& original, const std::vector<DATATYPE>& Res, const std::string& vector_name) {
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

#define VERIFY
void print_usage(std::string name) {
    cout << "Usage: " << name << " [n]" << endl;
}

void init_inverse_map(int num_points, int num_zones, int num_corners) {
    p_to_c_map.clear();
    p_to_c_map.resize(num_points);
    z_to_c_map.clear();
    z_to_c_map.resize(num_zones);
    for (int c = 0; c < num_corners; ++c) {
        int p = c_to_p_map[c];
        int z = c_to_z_map[c];
        p_to_c_map[p].push_back(c);
        z_to_c_map[z].push_back(c);
    }
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

    point_volume.resize(num_points );
    zone_gradient.resize(num_zones);
    point_gradient.resize(num_points);
    
    zone_field.resize(num_zones);
    zone_volume.resize(num_zones);
    point_normal.resize(num_points);
    point_type.resize(num_points);
    zone_type.resize(num_zones);

    point_volume_exp.resize(num_points);
    point_gradient_exp.resize(num_points);
    zone_gradient_exp.resize(num_zones);
    zone_volume_exp.resize(num_zones);

    // Initialize point_type and zone_type (assuming all internal for simplicity)
    std::fill(point_type.begin(), point_type.end(), 1);
    std::fill(zone_type.begin(), zone_type.end(), 1);

    // Initialize data arrays with random values
    for (int i = 0; i < num_points; ++i) {
        point_type[i] = (rand() % 100 < branch_bias * 100) ? 1 : -1;
        zone_type[i] = (rand() % 100 < branch_bias * 100) ? 1 : -1;
    }
    std::fill(point_volume.begin(), point_volume.end(), 1.0);
    std::fill(point_gradient.begin(), point_gradient.end(), 1.0);
    std::fill(corner_volume.begin(), corner_volume.end(), 1.0);
    std::fill(point_normal.begin(), point_normal.end(), 1.0);
    std::fill(csurf.begin(), csurf.end(), 1.0);
    std::fill(zone_field.begin(), zone_field.end(), 1.0);
    std::fill(zone_volume.begin(), zone_volume.end(), 1.0);
    std::fill(zone_gradient.begin(), zone_gradient.end(), 1.0);

    std::fill(zone_gradient_exp.begin(), zone_gradient_exp.end(), 1.0);
    std::fill(point_volume_exp.begin(), point_volume_exp.end(), 1.0);
    std::fill(point_gradient_exp.begin(), point_gradient_exp.end(), 1.0);
    std::fill(zone_volume_exp.begin(), zone_volume_exp.end(), 1.0);

    // Initialize c_to_p_map and c_to_z_map with random valid indices
    for (int c = DISTANCE_OTEHRS; c < num_corners+DISTANCE_OTEHRS; ++c) {
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
            assert(false && "c_to_z_map[c] >= num_zones");;
    }

    init_inverse_map(num_points, num_zones, num_corners);

    init_inverse_map_CSR(num_points, num_zones, num_corners);

#ifndef MAA
// Run original functions
    gradzatz_invert();
#else
    alloc_MAA();
    init_MAA();

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
            tilesj[thread_id] = get_new_tile<int>();
            regs0[thread_id] = get_new_reg<int>();
            regs1[thread_id] = get_new_reg<int>();
            regs2[thread_id] = get_new_reg<int>();
            regs3[thread_id] = get_new_reg<int>();
            regs4[thread_id] = get_new_reg<int>();
            regs5[thread_id] = get_new_reg<int>();
            last_i_regs[thread_id] = get_new_reg<int>();
            last_j_regs[thread_id] = get_new_reg<int>();
        }
    }
    gradzatz_invert_CSR();
#endif

#ifdef VERIFY
    gradzatz_invert();
    verify_results(point_volume_exp, point_volume, "point_volume");
    verify_results(point_gradient_exp, point_gradient, "point_gradient");
    verify_results(zone_gradient_exp, zone_gradient, "zone_gradient");
#endif
    return 0;
}