// Copyright (c) 2015, The Regents of the University of California (Regents)
// See LICENSE.txt for license details

#include <algorithm>
#include <iostream>
#include <omp.h>
#include <vector>

#include "MAA.hpp"
#include "benchmark.h"
#include "builder.h"
#include "command_line.h"
#include "graph.h"
#include "pvector.h"
#include <string.h>

#if !defined(FUNC) && !defined(GEM5) && !defined(GEM5_MAGIC)
#define GEM5
#endif

#ifdef GEM5
#include <gem5/m5ops.h>
#endif

#if defined(FUNC)
#include <MAA_functional.hpp>
#elif defined(GEM5)
#include <MAA_gem5.hpp>
#include <gem5/m5ops.h>
#elif defined(GEM5_MAGIC)
#include "MAA_gem5_magic.hpp"
#endif

int tiles1[NUM_CORES], tiles2[NUM_CORES], tiles3[NUM_CORES], tiles5[NUM_CORES], tilesi[NUM_CORES], tilesj[NUM_CORES];
int regs0[NUM_CORES], regs1[NUM_CORES], regs2[NUM_CORES], regs3[NUM_CORES], regs4[NUM_CORES], regs5[NUM_CORES], last_i_regs[NUM_CORES], last_j_regs[NUM_CORES];

/*
GAP Benchmark Suite
Kernel: PageRank (PR)
Author: Scott Beamer

Will return pagerank scores for all vertices once total change < epsilon

This PR implementation uses the traditional iterative approach. It performs
updates in the pull direction to remove the need for atomics, and it allows
new values to be immediately visible (like Gauss-Seidel method). The prior PR
implementation is still available in src/pr_spmv.cc.
*/

using namespace std;

typedef float ScoreT;
const float kDamp = 0.85;

pvector<ScoreT> PageRankPullGS(const Graph &g, int max_iters, double epsilon = 0, bool logging_enabled = false) {
    int num_nodes = g.num_nodes();
    int num_edges = g.num_edges();
    std::cout << "PR: num_nodes: " << num_nodes << ", num_edges: " << num_edges << ", edge/node: " << (double)num_nodes / (double)num_edges << std::endl;

    const ScoreT init_score = 1.0f / num_nodes;
    const ScoreT base_score = (1.0f - kDamp) / num_nodes;
    pvector<ScoreT> scores(num_nodes, init_score);
    pvector<ScoreT> *outgoing_contribs[2];
    outgoing_contribs[0] = new pvector<ScoreT>(num_nodes);
    outgoing_contribs[1] = new pvector<ScoreT>(num_nodes);
    pvector<SGOffset> VertexOffsets = g.VertexOffsets(true);

    alloc_MAA();
    init_MAA();

#ifdef GEM5
#pragma omp parallel
    {
#pragma omp master
        {
            std::cout << "ROI started: " << omp_get_num_threads() << " threads" << std::endl;
            m5_work_begin(0, 0);
            m5_reset_stats(0, 0);
        }
    }
#endif

#pragma omp parallel for
    for (NodeID n = 0; n < num_nodes; n++)
        (*outgoing_contribs[0])[n] = init_score / g.out_degree(n);
    for (int iter = 0; iter < max_iters; iter++) {
        double error = 0;
        pvector<ScoreT> &curr_contrib = *outgoing_contribs[iter % 2];
        pvector<ScoreT> &next_contrib = *outgoing_contribs[(iter + 1) % 2];
#ifdef GEM5
        clear_mem_region();
        add_mem_region(VertexOffsets.beginp(), VertexOffsets.endp());                // 6
        add_mem_region(curr_contrib.beginp(), curr_contrib.endp());                  // 7
        add_mem_region(next_contrib.beginp(), next_contrib.endp());                  // 8
        add_mem_region(g.in_neighbors_, &g.in_neighbors_[VertexOffsets[num_nodes]]); // 9
        add_mem_region(scores.beginp(), scores.endp());                              // 10
#endif
#pragma omp parallel for reduction(+ : error) schedule(dynamic, 16384)
        for (NodeID u = 0; u < num_nodes; u++) {
            ScoreT incoming_total = 0;
            for (int j = VertexOffsets[u]; j < VertexOffsets[u + 1]; j++) {
                incoming_total += curr_contrib[g.in_neighbors_[j]];
            }
            ScoreT old_score = scores[u];
            scores[u] = base_score + kDamp * incoming_total;
            error += fabs(scores[u] - old_score);
            next_contrib[u] = scores[u] / g.out_degree(u);
        }
#ifdef GEM5
        clear_mem_region();
#endif
        if (logging_enabled)
            PrintStep(iter, error);
        if (error < epsilon)
            break;
    }
#ifdef GEM5
    clear_mem_region();
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
    std::cout << "ROI End!!!" << std::endl;
    m5_exit(0);
#endif
    return scores;
}

pvector<ScoreT> PageRankPullMAA(const Graph &g, int max_iters, double epsilon = 0, bool logging_enabled = false) {
    int num_nodes = g.num_nodes();
    int num_edges = g.num_edges();
    std::cout << "PR: num_nodes: " << num_nodes << ", num_edges: " << num_edges << ", edge/node: " << (double)num_nodes / (double)num_edges << std::endl;

    const ScoreT init_score = 1.0f / num_nodes;
    const ScoreT base_score = (1.0f - kDamp) / num_nodes;
    pvector<ScoreT> scores(num_nodes, init_score);
    pvector<ScoreT> *outgoing_contribs[2];
    outgoing_contribs[0] = new pvector<ScoreT>(num_nodes);
    outgoing_contribs[1] = new pvector<ScoreT>(num_nodes);
    pvector<SGOffset> VertexOffsets = g.VertexOffsets(true);

    ScoreT *incoming_totals = new ScoreT[num_nodes];
    memset(incoming_totals, 0, num_nodes * sizeof(ScoreT));

    alloc_MAA();
    init_MAA();

#pragma omp parallel
    {
#pragma omp critical
        {
            int tid = omp_get_thread_num();
            tiles1[tid] = get_new_tile<int>();
            tiles2[tid] = get_new_tile<int>();
            tiles3[tid] = get_new_tile<int>();
            tiles5[tid] = get_new_tile<int>();
            tilesi[tid] = get_new_tile<int>();
            tilesj[tid] = get_new_tile<int>();
            regs0[tid] = get_new_reg<int>();
            regs1[tid] = get_new_reg<int>();
            regs2[tid] = get_new_reg<int>();
            regs3[tid] = get_new_reg<int>();
            regs4[tid] = get_new_reg<int>();
            regs5[tid] = get_new_reg<int>();
            last_i_regs[tid] = get_new_reg<int>();
            last_j_regs[tid] = get_new_reg<int>();
        }
    }
#ifdef GEM5
#pragma omp parallel
    {
#pragma omp master
        {
            std::cout << "ROI started: " << omp_get_num_threads() << " threads" << std::endl;
            m5_work_begin(0, 0);
            m5_reset_stats(0, 0);
        }
    }
#endif

#pragma omp parallel for
    for (NodeID n = 0; n < num_nodes; n++)
        (*outgoing_contribs[0])[n] = init_score / g.out_degree(n);

    for (int iter = 0; iter < max_iters; iter++) {
        double error = 0;
        pvector<ScoreT> &curr_contrib = *outgoing_contribs[iter % 2];
        pvector<ScoreT> &next_contrib = *outgoing_contribs[(iter + 1) % 2];
#ifdef GEM5
        clear_mem_region();
        add_mem_region(VertexOffsets.beginp(), VertexOffsets.endp());                // 6
        add_mem_region(curr_contrib.beginp(), curr_contrib.endp());                  // 7
        add_mem_region(next_contrib.beginp(), next_contrib.endp());                  // 8
        add_mem_region(g.in_neighbors_, &g.in_neighbors_[VertexOffsets[num_nodes]]); // 9
        add_mem_region(scores.beginp(), scores.endp());                              // 10
        add_mem_region(incoming_totals, &incoming_totals[num_nodes]);                // 11
#endif
#pragma omp parallel
        {
            int tilelb, tileub, tile3, tile5, tilei, tilej;
            int reg0, reg1, regOne, j_start_reg, j_end_reg, last_i_reg, last_j_reg;
            int tid = omp_get_thread_num();
            tilelb = tiles1[tid];
            tileub = tiles2[tid];
            tile3 = tiles3[tid];
            tile5 = tiles5[tid];
            tilei = tilesi[tid];
            tilej = tilesj[tid];
            reg0 = regs0[tid];
            reg1 = regs1[tid];
            regOne = regs2[tid];
            j_start_reg = regs3[tid];
            j_end_reg = regs4[tid];
            last_i_reg = last_i_regs[tid];
            last_j_reg = last_j_regs[tid];

            maa_const<int>(1, regOne);
            maa_const<int>(num_nodes, reg1);

#pragma omp for schedule(dynamic) reduction(+ : error)
            for (int uidx = 0; uidx < num_nodes; uidx += TILE_SIZE) {
                maa_const<int>(uidx, reg0);
                // step1 load upper bounds of VertexOffsetsData using u
                maa_stream_load<int>(VertexOffsets.start_, reg0, reg1, regOne, tilelb);
                maa_stream_load<int>(&VertexOffsets.start_[1], reg0, reg1, regOne, tileub);
                // step2 do while using range loop api
                maa_const<int>(0, last_i_reg);
                maa_const<int>(-1, last_j_reg);
                ScoreT *scores_ptr = scores.data() + uidx;
                ScoreT *next_contrib_ptr = next_contrib.data() + uidx;
                ScoreT *incoming_total = incoming_totals + uidx;
                // step3 do while loop
                int j_max = VertexOffsets.start_[min(uidx + TILE_SIZE, num_nodes)];
                maa_const(j_max, j_end_reg);
                for (int j_base = VertexOffsets.start_[uidx]; j_base < j_max; j_base += TILE_SIZE) {
                    maa_const(j_base, j_start_reg);
                    maa_range_loop<int>(last_i_reg, last_j_reg, tilelb, tileub, regOne, tilei, tilej);
                    // first load g.in_neighbors_[j]
                    maa_stream_load<NodeID>(g.in_neighbors_, j_start_reg, j_end_reg, regOne, tile3);
                    // Transfer tilei, tile3
                    // then load curr_contrib[g.in_neighbors_[j]]
                    maa_indirect_load<ScoreT>(curr_contrib.data(), tile3, tile5);
                    // then do rmw for incoming_total[itile]
                    maa_indirect_rmw_vector<ScoreT>(incoming_total, tilei, tile5, Operation_t::ADD_OP);
                    wait_ready(tile3);
                }
                wait_ready(tile5);
#pragma omp simd simdlen(4)
                for (NodeID u = 0; u < min(num_nodes - uidx, TILE_SIZE); u++) {
                    ScoreT old_score = scores_ptr[u];
                    scores_ptr[u] = base_score + kDamp * incoming_total[u];
                    error += fabs(scores_ptr[u] - old_score);
                    next_contrib_ptr[u] = scores_ptr[u] / g.out_degree(u + uidx);
                }
            }
        }
#ifdef GEM5
        clear_mem_region();
#endif
        if (logging_enabled)
            PrintStep(iter, error);
        if (error < epsilon)
            break;
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
    std::cout << "ROI End!!!" << std::endl;
    m5_exit(0);
#endif
    return scores;
}

void PrintTopScores(const Graph &g, const pvector<ScoreT> &scores) {
    vector<pair<NodeID, ScoreT>> score_pairs(g.num_nodes());
    for (NodeID n = 0; n < g.num_nodes(); n++) {
        score_pairs[n] = make_pair(n, scores[n]);
    }
    int k = 5;
    vector<pair<ScoreT, NodeID>> top_k = TopK(score_pairs, k);
    for (auto kvp : top_k)
        cout << kvp.second << ":" << kvp.first << endl;
}

// Verifies by asserting a single serial iteration in push direction has
//   error < target_error
bool PRVerifier(const Graph &g, const pvector<ScoreT> &scores,
                double target_error) {
    const ScoreT base_score = (1.0f - kDamp) / g.num_nodes();
    pvector<ScoreT> incoming_sums(g.num_nodes(), 0);
    double error = 0;
    for (NodeID u : g.vertices()) {
        ScoreT outgoing_contrib = scores[u] / g.out_degree(u);
        for (NodeID v : g.out_neigh(u))
            incoming_sums[v] += outgoing_contrib;
    }
    for (NodeID n : g.vertices()) {
        error += fabs(base_score + kDamp * incoming_sums[n] - scores[n]);
        incoming_sums[n] = 0;
    }
    PrintTime("Total Error", error);
    return error < target_error;
}

int main(int argc, char *argv[]) {
    CLPageRank cli(argc, argv, "pagerank", 1e-4, 1);
    if (!cli.ParseArgs())
        return -1;
    Builder b(cli);
    Graph g = b.MakeGraph();
#ifdef GEM5
    std::cout << "Fake Checkpoint started" << std::endl;
    m5_checkpoint(0, 0);
    std::cout << "Fake Checkpoint ended" << std::endl;
#endif
    auto PRBound = [&cli](const Graph &g) {
#ifdef MAA
        return PageRankPullMAA(g, cli.max_iters(), cli.tolerance(), cli.logging_en());
#else
        return PageRankPullGS(g, cli.max_iters(), cli.tolerance(), cli.logging_en());
#endif
    };
    auto VerifierBound = [&cli](const Graph &g, const pvector<ScoreT> &scores) {
        return PRVerifier(g, scores, cli.tolerance());
    };
    BenchmarkKernel(cli, g, PRBound, PrintTopScores, VerifierBound);
    return 0;
}
