#include <iostream>

void gather_compiler(float *a, float *b, int *idx, int min, int max, const int C) {
    std::cout << "starting gather min(" << min << "), max(" << max << "), stride(" << 1 << ")" << std::endl;
#pragma scop
    for (int i = min; i < max; i++) {
        a[i] += C * b[idx[i]];
    }
#pragma endscop
}

void scatter_compiler(float *a, float *b, int *idx, int min, int max, const int C) {
    std::cout << "starting scatter min(" << min << "), max(" << max << "), stride(" << 1 << ")" << std::endl;
#pragma scop
    for (int i = min; i < max; i++) {
        a[idx[i]] = C * b[i];
        // std::cout << "a[idx[" << i << "](" << idx[i] << ")](" << a[idx[i]] << ") = " << C << " * b[" << i << "](" << b[i] << ")" << std::endl;
    }
#pragma endscop
}

void rmw_compiler(float *a, float *b, int *idx, int min, int max, const int C) {
    std::cout << "starting rmw min(" << min << "), max(" << max << "), stride(" << 1 << ")" << std::endl;
#pragma scop
    for (int i = min; i < max; i++) {
        a[idx[i]] += C * b[i];
    }
#pragma endscop
}

void gather_scatter_compiler(float *a, float *b, int *idx, int min, int max, const int C) {
    std::cout << "starting gather_scatter min(" << min << "), max(" << max << "), stride(" << 1 << ")" << std::endl;
#pragma scop
    for (int i = min; i < max; i++) {
        a[idx[i]] = C * b[idx[i]];
    }
#pragma endscop
}

void gather_rmw_compiler(float *a, float *b, int *idx, int min, int max, const int C) {
    std::cout << "starting gather_rmw min(" << min << "), max(" << max << "), stride(" << 1 << ")" << std::endl;
#pragma scop
    for (int i = min; i < max; i++) {
        a[idx[i]] += C * b[idx[i]];
    }
#pragma endscop
}

void gather_rmw_cond_compiler(float *a, float *b, int *idx, int min, int max, const float mul_const, float *cond, const float cond_const) {
    std::cout << "starting gather_rmw_cond min(" << min << "), max(" << max << "), stride(" << 1 << ")" << std::endl;
#pragma scop
    for (int i = min; i < max; i ++) {
        if (cond[i] > cond_const) {
            a[idx[i]] += mul_const * b[idx[i]];
        }
    }
#pragma endscop
}