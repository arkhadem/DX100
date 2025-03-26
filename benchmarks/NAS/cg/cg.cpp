/*
MIT License

Copyright (c) 2021 Parallel Applications Modelling Group - GMAP 
	GMAP website: https://gmap.pucrs.br
	
	Pontifical Catholic University of Rio Grande do Sul (PUCRS)
	Av. Ipiranga, 6681, Porto Alegre - Brazil, 90619-900

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

------------------------------------------------------------------------------

The original NPB 3.4.1 version was written in Fortran and belongs to: 
	http://www.nas.nasa.gov/Software/NPB/

Authors of the Fortran code:
	M. Yarrow
	C. Kuszmaul
	H. Jin

------------------------------------------------------------------------------

The serial C++ version is a translation of the original NPB 3.4.1
Serial C++ version: https://github.com/GMAP/NPB-CPP/tree/master/NPB-SER

Authors of the C++ code: 
	Dalvan Griebler <dalvangriebler@gmail.com>
	Gabriell Araujo <hexenoften@gmail.com>
 	Júnior Löff <loffjh@gmail.com>

------------------------------------------------------------------------------

The OpenMP version is a parallel implementation of the serial C++ version
OpenMP version: https://github.com/GMAP/NPB-CPP/tree/master/NPB-OMP

Authors of the OpenMP code:
	Júnior Löff <loffjh@gmail.com>
	
*/

#include "MAA.hpp"
#include <iostream>
#include <fstream>
#include <math.h>
#include <stdlib.h>
#include <omp.h>
#include <string>

#ifdef _OPENMP
#include "omp.h"
#endif

#if !defined(FUNC) && !defined(GEM5) && !defined(GEM5_MAGIC)
#define GEM5
#endif

#if defined(FUNC)
#include "MAA_functional.hpp"
#elif defined(GEM5)
#include "MAA_gem5.hpp"
#include <gem5/m5ops.h>
#elif defined(GEM5_MAGIC)
#include "MAA_gem5_magic.hpp"
#endif

// #define DO_VERIFY
#define MAA_VER 2
// Reduce cgitmax to 4 so we can run the benchmark in a reasonable time
#define CGITMAX 4
// Run for one iteration so GEM5 does not die!
#define NITER 1

/*
 * ---------------------------------------------------------------------
 * note: please observe that in the routine conj_grad_base three 
 * implementations of the sparse matrix-vector multiply have
 * been supplied. the default matrix-vector multiply is not
 * loop unrolled. the alternate implementations are unrolled
 * to a depth of 2 and unrolled to a depth of 8. please
 * experiment with these to find the fastest for your particular
 * architecture. if reporting timing results, any of these three may
 * be used without penalty.
 * ---------------------------------------------------------------------
 * class specific parameters: 
 * it appears here for reference only.
 * these are their values, however, this info is imported in the npbparams.h
 * include file, which is written by the sys/setparams.c program.
 * ---------------------------------------------------------------------
 */

typedef int boolean;

#define TRUE  1
#define FALSE 0

/*************/
/*  CLASS C  */
/*************/
#define NA     (37500 * NUM_CORES)
#define NONZER 15
#define SHIFT  110.0
#define RCOND  1.0e-1

#define NZ          (NA * (NONZER + 1) * (NONZER + 1))
#define NAZ         (NA * (NONZER + 1))
#define T_INIT      0
#define T_BENCH     1
#define T_CONJ_GRAD 2
#define T_LAST      3

/* global variables */
// Total: 26MB
#ifndef USE_DATA_FROM_FILE

// Total: 26MB
// 8MB
static int colidx[NZ];
// 56KB
static int rowstr[NA + 1];
// 56KB
static int iv[NA];
// 56KB
static int arow[NA];
// 672KB
static int acol[NAZ];
// 1.3MB
static double aelt[NAZ];
// 16MB
static double a[NZ];
#else
#if NUM_CORES == 4
#include "cg_data_4C.h"
#elif NUM_CORES == 8
#include "cg_data_8C.h"
#elif NUM_CORES == 16
#include "cg_data_16C.h"
#else
#error
#endif
#endif
// 112KB
static double x[NA + 2];
// 112KB
static double z[NA + 2];
// 112KB
static double p[NA + 2];
// 112KB
static double q[NA + 2];
// 112KB
static double r[NA + 2];
static int naa;
static int nzz;
static int firstrow;
static int lastrow;
static int firstcol;
static int lastcol;
static double amult;
static double tran;

/* function prototypes */
static void conj_grad_base(int colidx[], int rowstr[], double x[], double z[], double a[], double p[], double q[], double r[], double *rnorm);
static void conj_grad_maa(int colidx[], int rowstr[], double x[], double z[], double a[], double p[], double q[], double r[], double *rnorm);
static int icnvrt(double x, int ipwr2);
static void makea(int n, int nz, double a[], int colidx[], int rowstr[], int firstrow, int lastrow, int firstcol, int lastcol, int arow[], int acol[][NONZER + 1], double aelt[][NONZER + 1], int iv[]);
static void sparse(double a[], int colidx[], int rowstr[], int n, int nz, int nozer, int arow[], int acol[][NONZER + 1], double aelt[][NONZER + 1], int firstrow, int lastrow, int nzloc[], double rcond, double shift);
static void sprnvc(int n, int nz, int nn1, double v[], int iv[]);
static void vecset(int n, double v[], int iv[], int *nzv, int i, double val);

#if defined(USE_POW)
#define r23 pow(0.5, 23.0)
#define r46 (r23 * r23)
#define t23 pow(2.0, 23.0)
#define t46 (t23 * t23)
#else
#define r23 (0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5 * 0.5)
#define r46 (r23 * r23)
#define t23 (2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0 * 2.0)
#define t46 (t23 * t23)
#endif

double randlc(double *x, double a) {
    double t1, t2, t3, t4, a1, a2, x1, x2, z;

    /*
	 * ---------------------------------------------------------------------
	 * break A into two parts such that A = 2^23 * A1 + A2.
	 * ---------------------------------------------------------------------
	 */
    t1 = r23 * a;
    a1 = (int)t1;
    a2 = a - t23 * a1;

    /*
	 * ---------------------------------------------------------------------
	 * break X into two parts such that X = 2^23 * X1 + X2, compute
	 * Z = A1 * X2 + A2 * X1  (mod 2^23), and then
	 * X = 2^23 * Z + A2 * X2  (mod 2^46).
	 * ---------------------------------------------------------------------
	 */
    t1 = r23 * (*x);
    x1 = (int)t1;
    x2 = (*x) - t23 * x1;
    t1 = a1 * x2 + a2 * x1;
    t2 = (int)(r23 * t1);
    z = t1 - t23 * t2;
    t3 = t23 * z + a2 * x2;
    t4 = (int)(r46 * t3);
    (*x) = t3 - t46 * t4;

    return (r46 * (*x));
}

void save_data_to_file() {
#if NUM_CORES == 4
    std::ofstream outfile("cg_data_4C.h");
    if (!outfile.is_open()) {
        std::cerr << "Error opening file for writing: cg_data_4C.h" << std::endl;
        exit(1);
    }
#elif NUM_CORES == 8
    std::ofstream outfile("cg_data_8C.h");
    if (!outfile.is_open()) {
        std::cerr << "Error opening file for writing: cg_data_8C.h" << std::endl;
        exit(1);
    }
#elif NUM_CORES == 16
    std::ofstream outfile("cg_data_16C.h");
    if (!outfile.is_open()) {
        std::cerr << "Error opening file for writing: cg_data_16C.h" << std::endl;
        exit(1);
    }
#else
#error
#endif

    outfile << "#ifndef CG_DATA_H\n";
    outfile << "#define CG_DATA_H\n\n";

    // Write arrays
    outfile << "double a[NZ] = {";
    for (int i = 0; i < NZ; i++) {
        outfile << std::scientific << a[i];
        if (i != NZ - 1)
            outfile << ", ";
    }
    outfile << "};\n\n";

    outfile << "int colidx[NZ] = {";
    for (int i = 0; i < NZ; i++) {
        outfile << colidx[i];
        if (i != NZ - 1)
            outfile << ", ";
    }
    outfile << "};\n\n";

    outfile << "int rowstr[NA + 1] = {";
    for (int i = 0; i < NA + 1; i++) {
        outfile << rowstr[i];
        if (i != NA)
            outfile << ", ";
    }
    outfile << "};\n\n";

    outfile << "int arow[NA] = {";
    for (int i = 0; i < NA; i++) {
        outfile << arow[i];
        if (i != NA - 1)
            outfile << ", ";
    }
    outfile << "};\n\n";

    outfile << "int acol[NAZ] = {";
    for (int i = 0; i < NAZ; i++) {
        outfile << acol[i];
        if (i != NAZ - 1)
            outfile << ", ";
    }
    outfile << "};\n\n";

    outfile << "double aelt[NAZ] = {";
    for (int i = 0; i < NAZ; i++) {
        outfile << std::scientific << aelt[i];
        if (i != NAZ - 1)
            outfile << ", ";
    }
    outfile << "};\n\n";

    outfile << "int iv[NA] = {";
    for (int i = 0; i < NA; i++) {
        outfile << iv[i];
        if (i != NA - 1)
            outfile << ", ";
    }
    outfile << "};\n\n";

    outfile << "#endif // CG_DATA_H\n";
    outfile.close();
}

/* cg */
int main(int argc, char **argv) {

    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " [BASE|MAA]" << std::endl;
        return 1;
    }
    std::string mode = argv[1];
    std::cout << "Mode: " << mode << std::endl;

    int i, j, k, it;
    double zeta;
    double rnorm;
    double norm_temp1, norm_temp2;

    firstrow = 0;
    lastrow = NA - 1;
    firstcol = 0;
    lastcol = NA - 1;

#ifdef DO_VERIFY
    char class_npb;
    double zeta_verify_value;
    if (NA == 1400 && NONZER == 7 && NITER == 15 && SHIFT == 10.0) {
        class_npb = 'S';
        zeta_verify_value = 8.5971775078648;
    } else if (NA == 7000 && NONZER == 8 && NITER == 15 && SHIFT == 12.0) {
        class_npb = 'W';
        zeta_verify_value = 10.362595087124;
    } else if (NA == 14000 && NONZER == 11 && NITER == 15 && SHIFT == 20.0) {
        class_npb = 'A';
        zeta_verify_value = 17.130235054029;
    } else if (NA == 75000 && NONZER == 13 && NITER == 75 && SHIFT == 60.0) {
        class_npb = 'B';
        zeta_verify_value = 22.712745482631;
    } else if (NA == 150000 && NONZER == 15 && NITER == 75 && SHIFT == 110.0) {
        class_npb = 'C';
        zeta_verify_value = 28.973605592845;
    } else if (NA == 1500000 && NONZER == 21 && NITER == 100 && SHIFT == 500.0) {
        class_npb = 'D';
        zeta_verify_value = 52.514532105794;
    } else if (NA == 9000000 && NONZER == 26 && NITER == 100 && SHIFT == 1500.0) {
        class_npb = 'E';
        zeta_verify_value = 77.522164599383;
    } else {
        class_npb = 'U';
    }
#endif

    naa = NA;
    nzz = NZ;

    /* initialize random number generator */
    tran = 314159265.0;
    amult = 1220703125.0;
    zeta = randlc(&tran, amult);

#ifndef USE_DATA_FROM_FILE
    makea(naa, nzz, a, colidx, rowstr, firstrow, lastrow, firstcol, lastcol, arow, (int(*)[NONZER + 1])(void *)acol, (double(*)[NONZER + 1])(void *)aelt, iv);
#else
    std::cout << "Using data from file!" << std::endl;
#endif
#ifdef DUMP_TO_FILE
    save_data_to_file();
    std::cout << "Dumped data to file!" << std::endl;
    exit(0);
#endif

#pragma omp parallel private(it, i, j, k)
    {
#pragma omp for schedule(dynamic, 16) nowait
        for (k = 0; k < NZ; k++) {
            colidx[k] = colidx[k] - firstcol;
        }

/* set starting vector to (1, 1, .... 1) */
#pragma omp for schedule(dynamic, 8) nowait
        for (i = 0; i < NA + 1; i++) {
            x[i] = 1.0;
        }

#pragma omp for schedule(dynamic, 8) nowait
        for (j = 0; j < lastcol - firstcol + 1; j++) {
            q[j] = 0.0;
            z[j] = 0.0;
            r[j] = 0.0;
            p[j] = 0.0;
        }
    }

#ifdef GEM5
    m5_checkpoint(0, 0);
#endif

    std::cout << "\n\n NAS Parallel Benchmarks 4.1 Parallel C++ version with OpenMP - CG Benchmark" << std::endl;
    std::cout << " Size: " << NA << std::endl;
    std::cout << " Iterations: " << NITER << std::endl;
    std::cout << " NUM_CORES: " << NUM_CORES << std::endl;

    alloc_MAA();
    init_MAA();

    /*
	 * ---------------------------------------------------------------------
	 * note: as a result of the above call to makea:
	 * values of j used in indexing rowstr go from 0 --> lastrow-firstrow
	 * values of colidx which are col indexes go from firstcol --> lastcol
	 * so:
	 * shift the col index vals from actual (firstcol --> lastcol) 
	 * to local, i.e., (0 --> lastcol-firstcol)
	 * ---------------------------------------------------------------------
	 */
#pragma omp parallel private(it, i, j, k)
    {

#pragma omp single
        zeta = 0.0;

#ifdef GEM5
#pragma omp single
        {
            std::cout << "ROI started: " << omp_get_num_threads() << " threads" << std::endl;
            assert(omp_get_num_threads() == NUM_CORES);
            m5_work_begin(0, 0);
            m5_reset_stats(0, 0);
        }
#endif

        /*
		 * --------------------------------------------------------------------
		 * ---->
		 * main iteration for inverse power method
		 * ---->
		 * --------------------------------------------------------------------
		 */
        for (it = 1; it <= NITER; it++) {
            init_MAA();
            if (mode == "BASE")
                conj_grad_base(colidx, rowstr, x, z, a, p, q, r, &rnorm);
            else if (mode == "MAA")
                conj_grad_maa(colidx, rowstr, x, z, a, p, q, r, &rnorm);
            else {
                std::cerr << "Invalid mode: " << mode << ". Use 'BASE' or 'MAA'." << std::endl;
                exit(1);
#ifdef GEM5
                m5_exit(0);
#endif
            }

#pragma omp single
            {
                norm_temp1 = 0.0;
                norm_temp2 = 0.0;
            }

            /*
			 * --------------------------------------------------------------------
			 * zeta = shift + 1/(x.z)
			 * so, first: (x.z)
			 * also, find norm of z
			 * so, first: (z.z)
			 * --------------------------------------------------------------------
			 */
#pragma omp for reduction(+ : norm_temp1, norm_temp2)
            for (j = 0; j < lastcol - firstcol + 1; j++) {
                norm_temp1 += x[j] * z[j];
                norm_temp2 += z[j] * z[j];
            }
#pragma omp single
            {
                norm_temp2 = 1.0 / sqrt(norm_temp2);
                zeta = SHIFT + 1.0 / norm_temp1;
            }

#pragma omp master
            {
                if (it == 1) {
                    std::cout << "\n   iteration           ||r||                 zeta" << std::endl;
                }
                std::cout << "   " << it << "           " << rnorm << "                 " << zeta << std::endl;
            }
/* normalize z to obtain x */
#pragma omp for
            for (j = 0; j < lastcol - firstcol + 1; j++) {
                x[j] = norm_temp2 * z[j];
            }
        } /* end of main iter inv pow meth */
#ifdef GEM5
#pragma omp single
        {
            m5_dump_stats(0, 0);
            m5_work_end(0, 0);
            std::cout << "ROI End!!!" << std::endl;
            m5_exit(0);
        }
#endif
    } /* end parallel */

    /*
	 * --------------------------------------------------------------------
	 * end of timed section
	 * --------------------------------------------------------------------
	 */

    std::cout << " Benchmark completed" << std::endl;

#ifdef DO_VERIFY
    double epsilon = 1.0e-4;
    double err = 0;
    if (class_npb != 'U') {
        err = fabs(zeta - zeta_verify_value) / zeta_verify_value;
        if (err <= epsilon) {
            std::cout << " VERIFICATION SUCCESSFUL" << std::endl;
            std::cout << " Zeta is    " << zeta << std::endl;
            std::cout << " Error is   " << err << std::endl;
        } else {
            std::cout << " VERIFICATION FAILED" << std::endl;
            std::cout << " Zeta                " << zeta << std::endl;
            std::cout << " The correct zeta is " << zeta_verify_value << std::endl;
        }
    } else {
        std::cout << " Problem size unknown" << std::endl;
        std::cout << " NO VERIFICATION PERFORMED" << std::endl;
    }
#endif

    return 1;
#ifdef GEM5
    m5_exit(0);
#endif
}

/*
 * ---------------------------------------------------------------------
 * floating point arrays here are named as in NPB1 spec discussion of 
 * CG algorithm
 * ---------------------------------------------------------------------
 */
static void conj_grad_maa(int colidx[],
                          int rowstr[],
                          double x[],
                          double z[],
                          double a[],
                          double p[],
                          double q[],
                          double r[],
                          double *rnorm) {
    int j;
    int cgit, cgitmax;
    double alpha, beta, suml;
    static double d, sum, rho, rho0;
    int colidx_tile;
    int pz_tile;
    int stride_reg;
    int k_start_reg;
    int k_end_reg;
    double *pz_ptr;

    int tid = omp_get_thread_num();

#ifdef GEM5
#pragma omp single
    {
        clear_mem_region();
        add_mem_region(colidx, &colidx[NZ]);     // 6
        add_mem_region(rowstr, &rowstr[NA + 1]); // 7
        add_mem_region(a, &a[NZ]);               // 8
        add_mem_region(p, &p[NA + 2]);           // 9
        add_mem_region(q, &q[NA + 2]);           // 10
        add_mem_region(z, &z[NA + 2]);           // 11
        add_mem_region(r, &r[NA + 2]);           // 12
        add_mem_region(x, &x[NA + 2]);           // 13
    }
#endif

    cgitmax = CGITMAX;
#pragma omp single nowait
    {
        rho = 0.0;
        sum = 0.0;
    }

    /* initialize the CG algorithm */
    const int total_thread_iters = NUM_CORES * 8;
    const int naa_plus1 = naa + 1;
    const int naa_plus1_divisible_by_32 = (int)(naa_plus1 / total_thread_iters) * total_thread_iters;
    const int lastrow_firstrow_plus1 = lastrow - firstrow + 1;
    const int lastcol_firstcol_plus1 = lastcol - firstcol + 1;
    const int lastcol_firstcol_plus1_divisible_by_32 = (int)(lastcol_firstcol_plus1 / total_thread_iters) * total_thread_iters;
    const int lastrow_firstrow_plus1_divisible_by_64K = ((int)(lastrow_firstrow_plus1 / (NUM_CORES * TILE_SIZE))) * NUM_CORES * TILE_SIZE;
    const int tile_size = 1024;
    double *my_q = &q[tid * 8];
    double *my_z = &z[tid * 8];
    double *my_r = &r[tid * 8];
    double *my_p = &p[tid * 8];
    double *my_x = &x[tid * 8];

    /* initialize the CG algorithm */
    for (j = 0; j < naa_plus1_divisible_by_32; j += total_thread_iters) {
        my_q[j + 0] = my_z[j + 0] = 0.0;
        my_q[j + 1] = my_z[j + 1] = 0.0;
        my_q[j + 2] = my_z[j + 2] = 0.0;
        my_q[j + 3] = my_z[j + 3] = 0.0;
        my_q[j + 4] = my_z[j + 4] = 0.0;
        my_q[j + 5] = my_z[j + 5] = 0.0;
        my_q[j + 6] = my_z[j + 6] = 0.0;
        my_q[j + 7] = my_z[j + 7] = 0.0;
        my_p[j + 0] = my_r[j + 0] = my_x[j + 0];
        my_p[j + 1] = my_r[j + 1] = my_x[j + 1];
        my_p[j + 2] = my_r[j + 2] = my_x[j + 2];
        my_p[j + 3] = my_r[j + 3] = my_x[j + 3];
        my_p[j + 4] = my_r[j + 4] = my_x[j + 4];
        my_p[j + 5] = my_r[j + 5] = my_x[j + 5];
        my_p[j + 6] = my_r[j + 6] = my_x[j + 6];
        my_p[j + 7] = my_r[j + 7] = my_x[j + 7];
    }
#pragma omp for schedule(dynamic) nowait
    for (j = naa_plus1_divisible_by_32; j < naa_plus1; j++) {
        q[j] = z[j] = 0.0;
        p[j] = r[j] = x[j];
    }

    /*
	 * --------------------------------------------------------------------
	 * rho = r.r
	 * now, obtain the norm of r: First, sum squares of r elements locally...
	 * --------------------------------------------------------------------
	 */
    double rho_tmp = 0.0;
    for (j = 0; j < lastcol_firstcol_plus1_divisible_by_32; j += total_thread_iters) {
        rho_tmp += my_r[j + 0] * my_r[j + 0];
        rho_tmp += my_r[j + 1] * my_r[j + 1];
        rho_tmp += my_r[j + 2] * my_r[j + 2];
        rho_tmp += my_r[j + 3] * my_r[j + 3];
        rho_tmp += my_r[j + 4] * my_r[j + 4];
        rho_tmp += my_r[j + 5] * my_r[j + 5];
        rho_tmp += my_r[j + 6] * my_r[j + 6];
        rho_tmp += my_r[j + 7] * my_r[j + 7];
    }
#pragma omp for schedule(dynamic) nowait
    for (j = lastcol_firstcol_plus1_divisible_by_32; j < lastcol_firstcol_plus1; j++) {
        rho_tmp += r[j] * r[j];
    }
#pragma omp critical
    {
        rho += rho_tmp;
        pz_tile = get_new_tile<double>();
        colidx_tile = get_new_tile<int>();
        pz_ptr = get_cacheable_tile_pointer<double>(pz_tile);
        stride_reg = get_new_reg<int>(1);
        k_start_reg = get_new_reg<int>();
        k_end_reg = get_new_reg<int>();
    }

#pragma omp barrier

    /* the conj grad iteration loop */
    for (cgit = 1; cgit <= cgitmax; cgit++) {
#pragma omp master
        {
            std::cout << "cgit: " << cgit << std::endl;
        }

        /*
		 * ---------------------------------------------------------------------
		 * q = A.p
		 * the partition submatrix-vector multiply: use workspace w
		 * ---------------------------------------------------------------------
		 * 
		 * note: this version of the multiply is actually (slightly: maybe %5) 
		 * faster on the sp2 on 16 nodes than is the unrolled-by-2 version 
		 * below. on the Cray t3d, the reverse is TRUE, i.e., the 
		 * unrolled-by-two version is some 10% faster.  
		 * the unrolled-by-8 version below is significantly faster
		 * on the Cray t3d - overall speed of code is 1.5 times faster.
		 */

#pragma omp single nowait
        {
            d = 0.0;
            rho0 = rho;
            rho = 0.0;
        }

        // LOOP 1
        // for (j = 0; j < lastrow - firstrow + 1; j++) {
        //     suml = 0.0;
        //     for (k = rowstr[j]; k < rowstr[j + 1]; k++) {
        //         suml += a[k] * p[colidx[k]];
        //     }
        //     q[j] = suml;
        // }

#pragma omp for nowait
        for (int j_base = 0; j_base < lastrow_firstrow_plus1_divisible_by_64K; j_base += TILE_SIZE) {
            int j_max = j_base + TILE_SIZE < lastrow_firstrow_plus1_divisible_by_64K ? j_base + TILE_SIZE : lastrow_firstrow_plus1_divisible_by_64K;
            int j_curr = j_base;
            int k_base = rowstr[j_base];
            int k_max = rowstr[j_max];
            int curr_max_row_str = rowstr[j_base + 1];
            double suml = 0.0;
            maa_const(k_max, k_end_reg);
            for (; k_base < k_max; k_base += TILE_SIZE) {
                maa_const(k_base, k_start_reg);
                maa_stream_load<int>(colidx, k_start_reg, k_end_reg, stride_reg, colidx_tile);
                // Transfer colidx_tile
                maa_indirect_load<double>(p, colidx_tile, pz_tile);
                int curr_tilek_size = k_max - k_base < TILE_SIZE ? k_max - k_base : TILE_SIZE;
                int k_curr = k_base;
                wait_ready(pz_tile);
                for (int i = 0; i < curr_tilek_size; i++) {
                    if (k_curr == curr_max_row_str) {
                        q[j_curr] = suml;
                        // std::cout << "q[" << j_curr << "] = " << suml << std::endl;
                        suml = 0.0;
                        j_curr++;
                        curr_max_row_str = rowstr[j_curr + 1];
                    }
                    // std::cout << "suml(" << suml << ") += a[" << k_curr << "](" << a[k_curr] << ") * p[colidx[" << k_curr << "](" << colidx[k_curr] << ")](" << pz_ptr[i] << " or " << p[colidx[k_curr]] << ")" << std::endl;
                    suml += a[k_curr] * pz_ptr[i];
                    k_curr++;
                }
            }
            q[j_curr] = suml;
            // std::cout << "q[" << j_curr << "] = " << suml << std::endl;
        }

#pragma omp for schedule(dynamic)
        for (int j_base = lastrow_firstrow_plus1_divisible_by_64K; j_base < lastrow_firstrow_plus1; j_base += tile_size) {
            int j_max = j_base + tile_size < lastrow_firstrow_plus1 ? j_base + tile_size : lastrow_firstrow_plus1;
            int j_curr = j_base;
            int k_base = rowstr[j_base];
            int k_max = rowstr[j_max];
            int curr_max_row_str = rowstr[j_base + 1];
            double suml = 0.0;
            maa_const(k_max, k_end_reg);
            for (; k_base < k_max; k_base += TILE_SIZE) {
                maa_const(k_base, k_start_reg);
                maa_stream_load<int>(colidx, k_start_reg, k_end_reg, stride_reg, colidx_tile);
                maa_indirect_load<double>(p, colidx_tile, pz_tile);
                int curr_tilek_size = k_max - k_base < TILE_SIZE ? k_max - k_base : TILE_SIZE;
                int k_curr = k_base;
                wait_ready(pz_tile);
                for (int i = 0; i < curr_tilek_size; i++) {
                    if (k_curr == curr_max_row_str) {
                        q[j_curr] = suml;
                        // std::cout << "q[" << j_curr << "] = " << suml << std::endl;
                        suml = 0.0;
                        j_curr++;
                        curr_max_row_str = rowstr[j_curr + 1];
                    }
                    // std::cout << "suml(" << suml << ") += a[" << k_curr << "](" << a[k_curr] << ") * p[colidx[" << k_curr << "](" << colidx[k_curr] << ")](" << pz_ptr[i] << " or " << p[colidx[k_curr]] << ")" << std::endl;
                    suml += a[k_curr] * pz_ptr[i];
                    k_curr++;
                }
            }
            q[j_curr] = suml;
            // std::cout << "q[" << j_curr << "] = " << suml << std::endl;
        }
#pragma omp barrier

        /*
		 * --------------------------------------------------------------------
		 * obtain p.q
		 * --------------------------------------------------------------------
		 */

        double d_tmp = 0.0;
        for (j = 0; j < lastcol_firstcol_plus1_divisible_by_32; j += total_thread_iters) {
            d_tmp += my_p[j + 0] * my_q[j + 0];
            d_tmp += my_p[j + 1] * my_q[j + 1];
            d_tmp += my_p[j + 2] * my_q[j + 2];
            d_tmp += my_p[j + 3] * my_q[j + 3];
            d_tmp += my_p[j + 4] * my_q[j + 4];
            d_tmp += my_p[j + 5] * my_q[j + 5];
            d_tmp += my_p[j + 6] * my_q[j + 6];
            d_tmp += my_p[j + 7] * my_q[j + 7];
        }
#pragma omp for schedule(dynamic) nowait
        for (j = lastcol_firstcol_plus1_divisible_by_32; j < lastcol_firstcol_plus1; j++) {
            d_tmp += p[j] * q[j];
        }
#pragma omp critical
        {
            d += d_tmp;
        }
#pragma omp barrier
        /*
		 * --------------------------------------------------------------------
		 * obtain alpha = rho / (p.q)
		 * -------------------------------------------------------------------
		 */
        alpha = rho0 / d;
        // std::cout << "alpha (" << alpha << ") = rho0 (" << rho0 << ") / d (" << d << ")" << std::endl;

        /*
		 * ---------------------------------------------------------------------
		 * obtain z = z + alpha*p
		 * and    r = r - alpha*q
		 * ---------------------------------------------------------------------
		 */

        double rho_tmp = 0.0;
        for (j = 0; j < lastcol_firstcol_plus1_divisible_by_32; j += total_thread_iters) {
            my_z[j + 0] += alpha * my_p[j + 0];
            my_z[j + 1] += alpha * my_p[j + 1];
            my_z[j + 2] += alpha * my_p[j + 2];
            my_z[j + 3] += alpha * my_p[j + 3];
            my_z[j + 4] += alpha * my_p[j + 4];
            my_z[j + 5] += alpha * my_p[j + 5];
            my_z[j + 6] += alpha * my_p[j + 6];
            my_z[j + 7] += alpha * my_p[j + 7];
            my_r[j + 0] -= alpha * my_q[j + 0];
            my_r[j + 1] -= alpha * my_q[j + 1];
            my_r[j + 2] -= alpha * my_q[j + 2];
            my_r[j + 3] -= alpha * my_q[j + 3];
            my_r[j + 4] -= alpha * my_q[j + 4];
            my_r[j + 5] -= alpha * my_q[j + 5];
            my_r[j + 6] -= alpha * my_q[j + 6];
            my_r[j + 7] -= alpha * my_q[j + 7];
            rho_tmp += my_r[j + 0] * my_r[j + 0];
            rho_tmp += my_r[j + 1] * my_r[j + 1];
            rho_tmp += my_r[j + 2] * my_r[j + 2];
            rho_tmp += my_r[j + 3] * my_r[j + 3];
            rho_tmp += my_r[j + 4] * my_r[j + 4];
            rho_tmp += my_r[j + 5] * my_r[j + 5];
            rho_tmp += my_r[j + 6] * my_r[j + 6];
            rho_tmp += my_r[j + 7] * my_r[j + 7];
        }
#pragma omp for schedule(dynamic) nowait
        for (j = lastcol_firstcol_plus1_divisible_by_32; j < lastcol_firstcol_plus1; j++) {
            z[j] += alpha * p[j];
            r[j] -= alpha * q[j];
            rho_tmp += r[j] * r[j];
        }
#pragma omp critical
        {
            rho += rho_tmp;
        }
#pragma omp barrier

        beta = rho / rho0;
        // std::cout << "beta (" << beta << ") = rho (" << rho << ") / rho0 (" << rho0 << ")" << std::endl;

        /*
		 * ---------------------------------------------------------------------
		 * p = r + beta*p
		 * ---------------------------------------------------------------------
		 */
        for (j = 0; j < lastcol_firstcol_plus1_divisible_by_32; j += total_thread_iters) {
            my_p[j + 0] = my_r[j + 0] + beta * my_p[j + 0];
            my_p[j + 1] = my_r[j + 1] + beta * my_p[j + 1];
            my_p[j + 2] = my_r[j + 2] + beta * my_p[j + 2];
            my_p[j + 3] = my_r[j + 3] + beta * my_p[j + 3];
            my_p[j + 4] = my_r[j + 4] + beta * my_p[j + 4];
            my_p[j + 5] = my_r[j + 5] + beta * my_p[j + 5];
            my_p[j + 6] = my_r[j + 6] + beta * my_p[j + 6];
            my_p[j + 7] = my_r[j + 7] + beta * my_p[j + 7];
        }
#pragma omp for schedule(dynamic)
        for (j = lastcol_firstcol_plus1_divisible_by_32; j < lastcol_firstcol_plus1; j++) {
            p[j] = r[j] + beta * p[j];
        }
    } /* end of do cgit=1, cgitmax */

    /*
	 * ---------------------------------------------------------------------
	 * compute residual norm explicitly: ||r|| = ||x - A.z||
	 * first, form A.z
	 * the partition submatrix-vector multiply
	 * ---------------------------------------------------------------------
	 */
    // LOOP 2
    // #pragma omp for nowait
    //     for (j = 0; j < lastrow - firstrow + 1; j++) {
    //         suml = 0.0;
    //         for (k = rowstr[j]; k < rowstr[j + 1]; k++) {
    //             suml += a[k] * z[colidx[k]];
    //         }
    //         r[j] = suml;
    //     }

#pragma omp for nowait
    for (int j_base = 0; j_base < lastrow_firstrow_plus1_divisible_by_64K; j_base += TILE_SIZE) {
        int j_max = j_base + TILE_SIZE < lastrow_firstrow_plus1_divisible_by_64K ? j_base + TILE_SIZE : lastrow_firstrow_plus1_divisible_by_64K;
        int j_curr = j_base;
        int k_base = rowstr[j_base];
        int k_max = rowstr[j_max];
        int curr_max_row_str = rowstr[j_base + 1];
        double suml = 0.0;
        maa_const(k_max, k_end_reg);
        for (; k_base < k_max; k_base += TILE_SIZE) {
            maa_const(k_base, k_start_reg);
            maa_stream_load<int>(colidx, k_start_reg, k_end_reg, stride_reg, colidx_tile);
            maa_indirect_load<double>(z, colidx_tile, pz_tile);
            int curr_tilek_size = k_max - k_base < TILE_SIZE ? k_max - k_base : TILE_SIZE;
            int k_curr = k_base;
            wait_ready(pz_tile);
            for (int i = 0; i < curr_tilek_size; i++) {
                if (k_curr == curr_max_row_str) {
                    r[j_curr] = suml;
                    // std::cout << "r[" << j_curr << "] = " << suml << std::endl;
                    suml = 0.0;
                    j_curr++;
                    curr_max_row_str = rowstr[j_curr + 1];
                }
                // std::cout << "suml(" << suml << ") += a[" << k_curr << "](" << a[k_curr] << ") * p[colidx[" << k_curr << "](" << colidx[k_curr] << ")](" << pz_ptr[i] << " or " << p[colidx[k_curr]] << ")" << std::endl;
                suml += a[k_curr] * pz_ptr[i];
                k_curr++;
            }
        }
        r[j_curr] = suml;
        // std::cout << "r[" << j_curr << "] = " << suml << std::endl;
    }

#pragma omp for schedule(dynamic)
    for (int j_base = lastrow_firstrow_plus1_divisible_by_64K; j_base < lastrow_firstrow_plus1; j_base += tile_size) {
        int j_max = j_base + tile_size < lastrow_firstrow_plus1 ? j_base + tile_size : lastrow_firstrow_plus1;
        int j_curr = j_base;
        int k_base = rowstr[j_base];
        int k_max = rowstr[j_max];
        int curr_max_row_str = rowstr[j_base + 1];
        double suml = 0.0;
        maa_const(k_max, k_end_reg);
        for (; k_base < k_max; k_base += TILE_SIZE) {
            maa_const(k_base, k_start_reg);
            maa_stream_load<int>(colidx, k_start_reg, k_end_reg, stride_reg, colidx_tile);
            maa_indirect_load<double>(z, colidx_tile, pz_tile);
            int curr_tilek_size = k_max - k_base < TILE_SIZE ? k_max - k_base : TILE_SIZE;
            int k_curr = k_base;
            wait_ready(pz_tile);
            for (int i = 0; i < curr_tilek_size; i++) {
                if (k_curr == curr_max_row_str) {
                    r[j_curr] = suml;
                    // std::cout << "r[" << j_curr << "] = " << suml << std::endl;
                    suml = 0.0;
                    j_curr++;
                    curr_max_row_str = rowstr[j_curr + 1];
                }
                // std::cout << "suml(" << suml << ") += a[" << k_curr << "](" << a[k_curr] << ") * p[colidx[" << k_curr << "](" << colidx[k_curr] << ")](" << pz_ptr[i] << " or " << p[colidx[k_curr]] << ")" << std::endl;
                suml += a[k_curr] * pz_ptr[i];
                k_curr++;
            }
        }
        r[j_curr] = suml;
        // std::cout << "r[" << j_curr << "] = " << suml << std::endl;
    }
#pragma omp barrier

    double sum_tmp = 0.0;
    for (j = 0; j < lastcol_firstcol_plus1_divisible_by_32; j += total_thread_iters) {
        suml = x[j] - r[j];
        sum_tmp += suml * suml;
        suml = x[j + 1] - r[j + 1];
        sum_tmp += suml * suml;
        suml = x[j + 2] - r[j + 2];
        sum_tmp += suml * suml;
        suml = x[j + 3] - r[j + 3];
        sum_tmp += suml * suml;
        suml = x[j + 4] - r[j + 4];
        sum_tmp += suml * suml;
        suml = x[j + 5] - r[j + 5];
        sum_tmp += suml * suml;
        suml = x[j + 6] - r[j + 6];
        sum_tmp += suml * suml;
        suml = x[j + 7] - r[j + 7];
        sum_tmp += suml * suml;
    }
#pragma omp for schedule(dynamic) nowait
    for (j = lastcol_firstcol_plus1_divisible_by_32; j < lastcol_firstcol_plus1; j++) {
        suml = x[j] - r[j];
        sum_tmp += suml * suml;
    }
#pragma omp critical
    {
        sum += sum_tmp;
    }
#pragma omp barrier

#pragma omp single
    *rnorm = sqrt(sum);

#ifdef GEM5
#pragma omp single
    {
        clear_mem_region();
    }
#endif
}

/*
 * ---------------------------------------------------------------------
 * floating point arrays here are named as in NPB1 spec discussion of 
 * CG algorithm
 * ---------------------------------------------------------------------
 */
static void conj_grad_base(int colidx[],
                           int rowstr[],
                           double x[],
                           double z[],
                           double a[],
                           double p[],
                           double q[],
                           double r[],
                           double *rnorm) {
    int j, k;
    int cgit, cgitmax;
    double alpha, beta, suml;
    static double d, sum, rho, rho0;

#ifdef GEM5
#pragma omp single
    {
        clear_mem_region();
        add_mem_region(colidx, &colidx[NZ]);     // 6
        add_mem_region(rowstr, &rowstr[NA + 1]); // 7
        add_mem_region(a, &a[NZ]);               // 8
        add_mem_region(p, &p[NA + 2]);           // 9
        add_mem_region(q, &q[NA + 2]);           // 10
        add_mem_region(z, &z[NA + 2]);           // 11
        add_mem_region(r, &r[NA + 2]);           // 12
        add_mem_region(x, &x[NA + 2]);           // 13
    }
#endif

    cgitmax = CGITMAX;
#pragma omp single nowait
    {
        rho = 0.0;
        sum = 0.0;
    }
    /* initialize the CG algorithm */
#pragma omp for schedule(dynamic, 8)
    for (j = 0; j < naa + 1; j++) {
        q[j] = 0.0;
        z[j] = 0.0;
        r[j] = x[j];
        p[j] = r[j];
    }

    /*
	 * --------------------------------------------------------------------
	 * rho = r.r
	 * now, obtain the norm of r: First, sum squares of r elements locally...
	 * --------------------------------------------------------------------
	 */
#pragma omp for reduction(+ : rho)
    for (j = 0; j < lastcol - firstcol + 1; j++) {
        rho += r[j] * r[j];
    }

    /* the conj grad iteration loop */
    for (cgit = 1; cgit <= cgitmax; cgit++) {
        /*
		 * ---------------------------------------------------------------------
		 * q = A.p
		 * the partition submatrix-vector multiply: use workspace w
		 * ---------------------------------------------------------------------
		 * 
		 * note: this version of the multiply is actually (slightly: maybe %5) 
		 * faster on the sp2 on 16 nodes than is the unrolled-by-2 version 
		 * below. on the Cray t3d, the reverse is TRUE, i.e., the 
		 * unrolled-by-two version is some 10% faster.  
		 * the unrolled-by-8 version below is significantly faster
		 * on the Cray t3d - overall speed of code is 1.5 times faster.
		 */

#pragma omp single nowait
        {
            d = 0.0;
            /*
			 * --------------------------------------------------------------------
			 * save a temporary of rho
			 * --------------------------------------------------------------------
			 */
            rho0 = rho;
            rho = 0.0;
        }

        // LOOP 1
#pragma omp for nowait
        for (j = 0; j < lastrow - firstrow + 1; j++) {
            suml = 0.0;
            for (k = rowstr[j]; k < rowstr[j + 1]; k++) {
                // std::cout << "suml(" << suml << ") += a[" << k << "](" << a[k] << ") * p[colidx[" << k << "](" << colidx[k] << ")] (" << p[colidx[k]] << ")" << std::endl;
                suml += a[k] * p[colidx[k]];
            }
            q[j] = suml;
            // std::cout << "q[" << j << "] = " << suml << std::endl;
        }

        /*
		 * --------------------------------------------------------------------
		 * obtain p.q
		 * --------------------------------------------------------------------
		 */

#pragma omp for reduction(+ : d)
        for (j = 0; j < lastcol - firstcol + 1; j++) {
            d += p[j] * q[j];
        }

        /*
		 * --------------------------------------------------------------------
		 * obtain alpha = rho / (p.q)
		 * -------------------------------------------------------------------
		 */
        alpha = rho0 / d;
        // std::cout << "alpha (" << alpha << ") = rho0 (" << rho0 << ") / d (" << d << ")" << std::endl;

        /*
		 * ---------------------------------------------------------------------
		 * obtain z = z + alpha*p
		 * and    r = r - alpha*q
		 * ---------------------------------------------------------------------
		 */

#pragma omp for reduction(+ : rho)
        for (j = 0; j < lastcol - firstcol + 1; j++) {
            z[j] += alpha * p[j];
            r[j] -= alpha * q[j];

            /*
			 * ---------------------------------------------------------------------
			 * rho = r.r
			 * now, obtain the norm of r: first, sum squares of r elements locally...
			 * ---------------------------------------------------------------------
			 */
            rho += r[j] * r[j];
        }

        /*
		 * ---------------------------------------------------------------------
		 * obtain beta
		 * ---------------------------------------------------------------------
		 */
        beta = rho / rho0;
        // std::cout << "beta (" << beta << ") = rho (" << rho << ") / rho0 (" << rho0 << ")" << std::endl;

/*
		 * ---------------------------------------------------------------------
		 * p = r + beta*p
		 * ---------------------------------------------------------------------
		 */
#pragma omp for
        for (j = 0; j < lastcol - firstcol + 1; j++) {
            p[j] = r[j] + beta * p[j];
        }
    } /* end of do cgit=1, cgitmax */

    /*
	 * ---------------------------------------------------------------------
	 * compute residual norm explicitly: ||r|| = ||x - A.z||
	 * first, form A.z
	 * the partition submatrix-vector multiply
	 * ---------------------------------------------------------------------
	 */
    // LOOP 2
#pragma omp for nowait
    for (j = 0; j < lastrow - firstrow + 1; j++) {
        suml = 0.0;
        for (k = rowstr[j]; k < rowstr[j + 1]; k++) {
            suml += a[k] * z[colidx[k]];
        }
        r[j] = suml;
        // std::cout << "r[" << j << "] = " << suml << std::endl;
    }

/*
	 * ---------------------------------------------------------------------
	 * at this point, r contains A.z
	 * ---------------------------------------------------------------------
	 */
#pragma omp for reduction(+ : sum)
    for (j = 0; j < lastcol - firstcol + 1; j++) {
        suml = x[j] - r[j];
        sum += suml * suml;
    }

#pragma omp single
    *rnorm = sqrt(sum);

#ifdef GEM5
#pragma omp single
    {
        clear_mem_region();
    }
#endif
}

/*
 * ---------------------------------------------------------------------
 * scale a double precision number x in (0,1) by a power of 2 and chop it
 * ---------------------------------------------------------------------
 */
static int icnvrt(double x, int ipwr2) {
    return (int)(ipwr2 * x);
}

/*
 * ---------------------------------------------------------------------
 * generate the test problem for benchmark 6
 * makea generates a sparse matrix with a
 * prescribed sparsity distribution
 *
 * parameter    type        usage
 *
 * input
 *
 * n            i           number of cols/rows of matrix
 * nz           i           nonzeros as declared array size
 * rcond        r*8         condition number
 * shift        r*8         main diagonal shift
 *
 * output
 *
 * a            r*8         array for nonzeros
 * colidx       i           col indices
 * rowstr       i           row pointers
 *
 * workspace
 *
 * iv, arow, acol i
 * aelt           r*8
 * ---------------------------------------------------------------------
 */
#ifndef USE_DATA_FROM_FILE
static void makea(int n, int nz, double a[], int colidx[], int rowstr[], int firstrow, int lastrow, int firstcol, int lastcol, int arow[], int acol[][NONZER + 1], double aelt[][NONZER + 1], int iv[]) {
    int iouter, ivelt, nzv, nn1;
    int ivc[NONZER + 1];
    double vc[NONZER + 1];

    /*
	 * --------------------------------------------------------------------
	 * nonzer is approximately  (int(sqrt(nnza /n)));
	 * --------------------------------------------------------------------
	 * nn1 is the smallest power of two not less than n
	 * --------------------------------------------------------------------
	 */
    nn1 = 1;
    do {
        nn1 = 2 * nn1;
    } while (nn1 < n);

    /*
	 * -------------------------------------------------------------------
	 * generate nonzero positions and save for the use in sparse
	 * -------------------------------------------------------------------
	 */
    for (iouter = 0; iouter < n; iouter++) {
        nzv = NONZER;
        sprnvc(n, nzv, nn1, vc, ivc);
        vecset(n, vc, ivc, &nzv, iouter + 1, 0.5);
        arow[iouter] = nzv;
        for (ivelt = 0; ivelt < nzv; ivelt++) {
            acol[iouter][ivelt] = ivc[ivelt] - 1;
            aelt[iouter][ivelt] = vc[ivelt];
        }
    }

    /*
	 * ---------------------------------------------------------------------
	 * ... make the sparse matrix from list of elements with duplicates
	 * (iv is used as  workspace)
	 * ---------------------------------------------------------------------
	 */
    sparse(a, colidx, rowstr, n, nz, NONZER, arow, acol, aelt, firstrow, lastrow, iv, RCOND, SHIFT);
}
#endif

/*
 * ---------------------------------------------------------------------
 * rows range from firstrow to lastrow
 * the rowstr pointers are defined for nrows = lastrow-firstrow+1 values
 * ---------------------------------------------------------------------
 */
static void sparse(double a[], int colidx[], int rowstr[], int n, int nz, int nozer, int arow[], int acol[][NONZER + 1], double aelt[][NONZER + 1], int firstrow, int lastrow, int nzloc[], double rcond, double shift) {
    int nrows;

    /*
	 * ---------------------------------------------------
	 * generate a sparse matrix from a list of
	 * [col, row, element] tri
	 * ---------------------------------------------------
	 */
    int i, j, j1, j2, nza, k, kk, nzrow, jcol;
    double size, scale, ratio, va;
    boolean goto_40;

    /*
	 * --------------------------------------------------------------------
	 * how many rows of result
	 * --------------------------------------------------------------------
	 */
    nrows = lastrow - firstrow + 1;

    /*
	 * --------------------------------------------------------------------
	 * ...count the number of triples in each row
	 * --------------------------------------------------------------------
	 */
    for (j = 0; j < nrows + 1; j++) {
        rowstr[j] = 0;
    }
    for (i = 0; i < n; i++) {
        for (nza = 0; nza < arow[i]; nza++) {
            j = acol[i][nza] + 1;
            rowstr[j] = rowstr[j] + arow[i];
        }
    }
    rowstr[0] = 0;
    for (j = 1; j < nrows + 1; j++) {
        rowstr[j] = rowstr[j] + rowstr[j - 1];
    }
    nza = rowstr[nrows] - 1;

    /*
	 * ---------------------------------------------------------------------
	 * ... rowstr(j) now is the location of the first nonzero
	 * of row j of a
	 * ---------------------------------------------------------------------
	 */
    if (nza > nz) {
        std::cout << "Space for matrix elements exceeded in sparse" << std::endl;
        std::cout << "nza, nzmax = " << nza << ", " << nz << std::endl;
        exit(-1);
    }

    /*
	 * ---------------------------------------------------------------------
	 * ... preload data pages
	 * ---------------------------------------------------------------------
	 */
    for (j = 0; j < nrows; j++) {
        for (k = rowstr[j]; k < rowstr[j + 1]; k++) {
            a[k] = 0.0;
            colidx[k] = -1;
        }
        nzloc[j] = 0;
    }

    /*
	 * ---------------------------------------------------------------------
	 * ... generate actual values by summing duplicates
	 * ---------------------------------------------------------------------
	 */
    size = 1.0;
    ratio = pow(rcond, (1.0 / (double)(n)));
    for (i = 0; i < n; i++) {
        for (nza = 0; nza < arow[i]; nza++) {
            j = acol[i][nza];

            scale = size * aelt[i][nza];
            for (nzrow = 0; nzrow < arow[i]; nzrow++) {
                jcol = acol[i][nzrow];
                va = aelt[i][nzrow] * scale;

                /*
				 * --------------------------------------------------------------------
				 * ... add the identity * rcond to the generated matrix to bound
				 * the smallest eigenvalue from below by rcond
				 * --------------------------------------------------------------------
				 */
                if (jcol == j && j == i) {
                    va = va + rcond - shift;
                }

                goto_40 = FALSE;
                for (k = rowstr[j]; k < rowstr[j + 1]; k++) {
                    if (colidx[k] > jcol) {
                        /*
						 * ----------------------------------------------------------------
						 * ... insert colidx here orderly
						 * ----------------------------------------------------------------
						 */
                        for (kk = rowstr[j + 1] - 2; kk >= k; kk--) {
                            if (colidx[kk] > -1) {
                                a[kk + 1] = a[kk];
                                colidx[kk + 1] = colidx[kk];
                            }
                        }
                        colidx[k] = jcol;
                        a[k] = 0.0;
                        goto_40 = TRUE;
                        break;
                    } else if (colidx[k] == -1) {
                        colidx[k] = jcol;
                        goto_40 = TRUE;
                        break;
                    } else if (colidx[k] == jcol) {
                        /*
						 * --------------------------------------------------------------
						 * ... mark the duplicated entry
						 * -------------------------------------------------------------
						 */
                        nzloc[j] = nzloc[j] + 1;
                        goto_40 = TRUE;
                        break;
                    }
                }
                if (goto_40 == FALSE) {
                    std::cout << "internal error in sparse: i=" << i << std::endl;
                    exit(-1);
                }
                a[k] = a[k] + va;
            }
        }
        size = size * ratio;
    }

    /*
	 * ---------------------------------------------------------------------
	 * ... remove empty entries and generate final results
	 * ---------------------------------------------------------------------
	 */
    for (j = 1; j < nrows; j++) {
        nzloc[j] = nzloc[j] + nzloc[j - 1];
    }

    for (j = 0; j < nrows; j++) {
        if (j > 0) {
            j1 = rowstr[j] - nzloc[j - 1];
        } else {
            j1 = 0;
        }
        j2 = rowstr[j + 1] - nzloc[j];
        nza = rowstr[j];
        for (k = j1; k < j2; k++) {
            a[k] = a[nza];
            colidx[k] = colidx[nza];
            nza = nza + 1;
        }
    }
    for (j = 1; j < nrows + 1; j++) {
        rowstr[j] = rowstr[j] - nzloc[j - 1];
    }
    nza = rowstr[nrows] - 1;
}

/*
 * ---------------------------------------------------------------------
 * generate a sparse n-vector (v, iv)
 * having nzv nonzeros
 *
 * mark(i) is set to 1 if position i is nonzero.
 * mark is all zero on entry and is reset to all zero before exit
 * this corrects a performance bug found by John G. Lewis, caused by
 * reinitialization of mark on every one of the n calls to sprnvc
 * ---------------------------------------------------------------------
 */
static void sprnvc(int n, int nz, int nn1, double v[], int iv[]) {
    int nzv, ii, i;
    double vecelt, vecloc;

    nzv = 0;

    while (nzv < nz) {
        vecelt = randlc(&tran, amult);

        /*
		 * --------------------------------------------------------------------
		 * generate an integer between 1 and n in a portable manner
		 * --------------------------------------------------------------------
		 */
        vecloc = randlc(&tran, amult);
        i = icnvrt(vecloc, nn1) + 1;
        if (i > n) {
            continue;
        }

        /*
		 * --------------------------------------------------------------------
		 * was this integer generated already?
		 * --------------------------------------------------------------------
		 */
        boolean was_gen = FALSE;
        for (ii = 0; ii < nzv; ii++) {
            if (iv[ii] == i) {
                was_gen = TRUE;
                break;
            }
        }
        if (was_gen) {
            continue;
        }
        v[nzv] = vecelt;
        iv[nzv] = i;
        nzv = nzv + 1;
    }
}

/*
 * --------------------------------------------------------------------
 * set ith element of sparse vector (v, iv) with
 * nzv nonzeros to val
 * --------------------------------------------------------------------
 */
static void vecset(int n, double v[], int iv[], int *nzv, int i, double val) {
    int k;
    boolean set;

    set = FALSE;
    for (k = 0; k < *nzv; k++) {
        if (iv[k] == i) {
            v[k] = val;
            set = TRUE;
        }
    }
    if (set == FALSE) {
        v[*nzv] = val;
        iv[*nzv] = i;
        *nzv = *nzv + 1;
    }
}
