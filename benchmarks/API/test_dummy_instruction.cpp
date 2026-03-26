#include "MAA.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>

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

// Dummy API harness for a future ISA instruction.
// Flow:
// 1. Compute a host-side reference result
// 2. Initialize MAA state and allocate the required registers and SPD tiles
// 3. Load source data into SPD and issue a placeholder MAA op
// 4. Replace the placeholder call with the real instruction wrapper once added
// 5. Wait for completion, copy the destination tile back, and compare results

template <class T>
void dummy_new_instruction_reference(const T *src, T *dst, int n, T bias) {
    for (int i = 0; i < n; i++) {
        dst[i] = src[i] + bias;
    }
}

template <class T>
void dummy_new_instruction_maa(const T *src, T *dst, int n, T bias) {
    init_MAA();

    int min_reg = get_new_reg<int>(0);
    int max_reg = get_new_reg<int>(n);
    int stride_reg = get_new_reg<int>(1);
    int bias_reg = get_new_reg<T>(bias);

    int src_tile = get_new_tile<T>();
    int dst_tile = get_new_tile<T>();

    maa_stream_load<T>(const_cast<T *>(src), min_reg, max_reg, stride_reg, src_tile);

    // TODO: replace this with the real new-instruction wrapper when it exists.
    // Example:
    // maa_new_instruction<T>(src_tile, bias_reg, dst_tile);
    maa_alu_scalar<T>(src_tile, bias_reg, dst_tile, Operation_t::ADD_OP);

    wait_ready(dst_tile);
    T *dst_tile_ptr = get_cacheable_tile_pointer<T>(dst_tile);
    for (int i = 0; i < n; i++) {
        dst[i] = dst_tile_ptr[i];
    }
}

template <class T>
bool compare_arrays(const T *expected, const T *actual, int n, double eps = 0.001) {
    for (int i = 0; i < n; i++) {
        if (std::abs(static_cast<double>(expected[i]) - static_cast<double>(actual[i])) > eps) {
            std::cout << "Mismatch at " << i << ": expected " << expected[i] << ", got " << actual[i] << std::endl;
            return false;
        }
    }
    return true;
}

int main(int argc, char *argv[]) {
    int n = argc > 1 ? std::atoi(argv[1]) : 64;
    if (n <= 0 || n > TILE_SIZE) {
        std::cout << "Usage: " << argv[0] << " [n], where 0 < n <= " << TILE_SIZE << std::endl;
#ifdef GEM5
        m5_exit(1);
#endif
        return 1;
    }

    alloc_MAA();

    float *src = static_cast<float *>(malloc(sizeof(float) * n));
    float *expected = static_cast<float *>(malloc(sizeof(float) * n));
    float *actual = static_cast<float *>(malloc(sizeof(float) * n));
    if (src == nullptr || expected == nullptr || actual == nullptr) {
        std::cout << "Allocation failed" << std::endl;
        free(src);
        free(expected);
        free(actual);
#ifdef GEM5
        m5_exit(1);
#endif
        return 1;
    }

    const float bias = 7.0f;
    for (int i = 0; i < n; i++) {
        src[i] = static_cast<float>(i) * 1.5f;
        expected[i] = 0.0f;
        actual[i] = 0.0f;
    }

    dummy_new_instruction_reference(src, expected, n, bias);
    dummy_new_instruction_maa(src, actual, n, bias);

    bool passed = compare_arrays(expected, actual, n);
    if (passed) {
        std::cout << "dummy_new_instruction_api correct" << std::endl;
    }

    free(src);
    free(expected);
    free(actual);

#ifdef GEM5
    m5_exit(passed ? 0 : 1);
#endif
    return passed ? 0 : 1;
}
