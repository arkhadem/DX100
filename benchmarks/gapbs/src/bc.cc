// Copyright (c) 2015, The Regents of the University of California (Regents)
// See LICENSE.txt for license details

#include <functional>
#include <iostream>
#include <vector>
#include <omp.h>

#include "MAA.hpp"
#include "benchmark.h"
#include "bitmap.h"
#include "builder.h"
#include "command_line.h"
#include "graph.h"
#include "platform_atomics.h"
#include "pvector.h"
#include "sliding_queue.h"
#include "timer.h"
#include "util.h"

#define MAA_FULL2

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
#include <MAA_utility.hpp>

/*
GAP Benchmark Suite
Kernel: Betweenness Centrality (BC)
Author: Scott Beamer

Will return array of approx betweenness centrality scores for each vertex

This BC implementation makes use of the Brandes [1] algorithm with
implementation optimizations from Madduri et al. [2]. It is only approximate
because it does not compute the paths from every start vertex, but only a small
subset of them. Additionally, the scores are normalized to the range [0,1].

As an optimization to save memory, this implementation uses a Bitmap to hold
succ (list of successors) found during the BFS phase that are used in the back-
propagation phase.

[1] Ulrik Brandes. "A faster algorithm for betweenness centrality." Journal of
    Mathematical Sociology, 25(2):163–177, 2001.

[2] Kamesh Madduri, David Ediger, Karl Jiang, David A Bader, and Daniel
    Chavarria-Miranda. "A faster parallel algorithm and efficient multithreaded
    implementations for evaluating betweenness centrality on massive datasets."
    International Symposium on Parallel & Distributed Processing (IPDPS), 2009.
*/

using namespace std;
typedef float ScoreT;
typedef float CountT;

int tiles0[NUM_CORES], tiles1[NUM_CORES], tiles2[NUM_CORES], tiles3[NUM_CORES], tiles4[NUM_CORES], tiles5[NUM_CORES], tiles6[NUM_CORES], tiles7[NUM_CORES];
int regs0[NUM_CORES], regs1[NUM_CORES], regs2[NUM_CORES], regs3[NUM_CORES], regs4[NUM_CORES], regs5[NUM_CORES], regs6[NUM_CORES], regs7[NUM_CORES];

void PBFS(const Graph &g, SGOffset *VertexOffsetsData, NodeID source, pvector<CountT> &path_counts,
          Bitmap &succ, vector<SlidingQueue<NodeID>::iterator> &depth_index,
          SlidingQueue<NodeID> &queue, pvector<NodeID> &depths) {
    depths.fill(-1);
    depths[source] = 0;
    path_counts[source] = 1;
    queue.push_back(source);
    depth_index.push_back(queue.begin());
    queue.slide_window();
    const NodeID *g_out_start = g.out_neigh(0).begin();

#pragma omp parallel
    {
        NodeID depth = 0;
        QueueBuffer<NodeID> lqueue(queue);
        while (!queue.empty()) {
#pragma omp master
            std::cout << "Starting PBFS: " << queue.size() << " elements" << std::endl;
            depth++;
// #pragma omp single
//             {
//                 std::vector<int32_t> qv;
//                 for (auto q_iter = queue.begin(); q_iter < queue.end(); q_iter++) {
//                     qv.push_back(*q_iter);
//                 }
//                 // sort qv
//                 std::sort(qv.begin(), qv.end());
//                 printf("depth %d: %zu elements\n", depth, queue.size());
//                 for (int i = 0; i < queue.size(); i++) {
//                     printf("[%d] %d\n", i, qv[i]);
//                 }
//             }
#pragma omp for schedule(dynamic, 64) nowait
            for (auto q_iter = queue.begin(); q_iter < queue.end(); q_iter++) {
                NodeID u = *q_iter;
                for (SGOffset vidx = VertexOffsetsData[u]; vidx < VertexOffsetsData[u + 1]; vidx++) {
                    NodeID &v = g.out_neighbors_[vidx];
                    if ((depths[v] == -1) &&
                        (compare_and_swap(depths[v], static_cast<NodeID>(-1), depth))) {
                        lqueue.push_back(v);
                    }
                    if (depths[v] == depth) {
                        succ.set_bit_atomic(&v - g_out_start);
#pragma omp atomic
                        path_counts[v] += path_counts[u];
                    }
                }
            }
            // std::cout << lqueue.in << std::endl;
            lqueue.flush();
#pragma omp barrier
#pragma omp single
            {
                depth_index.push_back(queue.begin());
                queue.slide_window();
            }
        }
    }
    depth_index.push_back(queue.begin());
}

void PBFSMAA(const Graph &g, SGOffset *VertexOffsetsData, NodeID source, pvector<CountT> &path_counts,
             Bitmap &succ, vector<int> &depth_index,
             SlidingQueue<NodeID> &queue, pvector<NodeID> &depths) {
    depths.fill(-1);
    depths[source] = 0;
    path_counts[source] = 1;
    queue.push_back(source);
    depth_index.push_back(queue.begin_idx());
    queue.slide_window();

#pragma omp parallel
    {
        int tilelb, tile0, tileub, tile1, tile4, tile5, tile3, tile2;
        int reg_const_1, reg_const_minus_one, regMin, regMax, regDepth, last_i_reg, last_j_reg;
        int tid;

#pragma omp critical
        {
            tid = omp_get_thread_num();
            tile0 = tiles0[tid];
            tile1 = tiles1[tid];
            tile2 = tiles2[tid];
            tile3 = tiles3[tid];
            tile4 = tiles4[tid];
            tile5 = tiles5[tid];
            tileub = tiles6[tid];
            tilelb = tiles7[tid];
            reg_const_1 = regs0[tid];
            reg_const_minus_one = regs1[tid];
            regMin = regs2[tid];
            regMax = regs3[tid];
            regDepth = regs4[tid];
            last_i_reg = regs5[tid];
            last_j_reg = regs6[tid];
            maa_const<int>(1, reg_const_1);
            maa_const<int>(-1, reg_const_minus_one);
        }
        NodeID depth = 0;
        QueueBuffer<NodeID> lqueue(queue);

        int *tile0_ptr = get_cacheable_tile_pointer<int>(tile0);
        int *tile1_ptr = get_cacheable_tile_pointer<int>(tile1);
        SGOffset *tile3_ptr = get_cacheable_tile_pointer<SGOffset>(tile3);
        NodeID *tile4_ptr = get_cacheable_tile_pointer<NodeID>(tile4);
        NodeID *tile2_ptr = get_cacheable_tile_pointer<NodeID>(tile2);
        while (!queue.empty()) {
            depth++;
            // regDepth used for depth
            maa_const<int>(depth, regDepth);
            NodeID *base = queue.shared + queue.shared_out_start;
            int len = queue.size();
            // #pragma omp single
            //             {
            //                 std::vector<int32_t> qv;
            //                 for (int i = 0; i < len; i++) {
            //                     qv.push_back(base[i]);
            //                 }
            //                 // sort qv
            //                 std::sort(qv.begin(), qv.end());
            //                 printf("depth %d: %d elements\n", depth, len);
            //                 for (int i = 0; i < len; i++) {
            //                     printf("[%d] %d\n", i, qv[i]);
            //                 }
            //             }
            int idx = 0;
            while (idx < len) {
                int current_len_size = len - idx;
#if TILE_SIZE == 32768
                const int tile_size = current_len_size > NUM_CORES * 32768   ? 32768
                                      : current_len_size > NUM_CORES * 16384 ? 16384
                                      : current_len_size > NUM_CORES * 8192  ? 8192
                                      : current_len_size > NUM_CORES * 4096  ? 4096
                                      : current_len_size > NUM_CORES * 2048  ? 2048
                                      : current_len_size > NUM_CORES * 1024  ? 1024
                                                                             : -1;
#elif TILE_SIZE == 16384
                const int tile_size = current_len_size > NUM_CORES * 16384  ? 16384
                                      : current_len_size > NUM_CORES * 8192 ? 8192
                                      : current_len_size > NUM_CORES * 4096 ? 4096
                                      : current_len_size > NUM_CORES * 2048 ? 2048
                                      : current_len_size > NUM_CORES * 1024 ? 1024
                                                                            : -1;
#elif TILE_SIZE == 8192
                const int tile_size = current_len_size > NUM_CORES * 8192   ? 8192
                                      : current_len_size > NUM_CORES * 4096 ? 4096
                                      : current_len_size > NUM_CORES * 2048 ? 2048
                                      : current_len_size > NUM_CORES * 1024 ? 1024
                                                                            : -1;
#elif TILE_SIZE == 4096
                const int tile_size = current_len_size > NUM_CORES * 4096   ? 4096
                                      : current_len_size > NUM_CORES * 2048 ? 2048
                                      : current_len_size > NUM_CORES * 1024 ? 1024
                                                                            : -1;
#elif TILE_SIZE == 2048
                const int tile_size = current_len_size > NUM_CORES * 2048   ? 2048
                                      : current_len_size > NUM_CORES * 1024 ? 1024
                                                                            : -1;
#elif TILE_SIZE == 1024
                const int tile_size = current_len_size > NUM_CORES * 1024 ? 1024
                                                                          : -1;
#else
                assert(false);
#endif
                if (tile_size != -1) {
                    int min = idx + tid * tile_size;
                    int max = min + tile_size;
#pragma omp critical
                    std::cout << "Starting PBFSMAA (d: " << depth << "): " << len << " elements, tile: " << tile_size << ", idx: " << idx << " (maa)" << std::endl;
                    maa_const<int>(min, regMin);
                    maa_const<int>(max, regMax);
                    // tile0 = u = base[i]
                    maa_stream_load<NodeID>(base, regMin, regMax, reg_const_1, tile0);
                    // tilelb = VertexOffsetsData[u]
                    maa_indirect_load<SGOffset>(VertexOffsetsData, tile0, tilelb);
                    // tileub = VertexOffsetsData[u+1]
                    maa_indirect_load<SGOffset>(&VertexOffsetsData[1], tile0, tileub);
                    int curr_tilej_size = 0;
                    maa_const<int>(0, last_i_reg);
                    maa_const<SGOffset>(-1, last_j_reg);
                    do {
                        // tile5 = i
                        // tile3 = vidx
                        maa_range_loop<SGOffset>(last_i_reg, last_j_reg, tilelb, tileub, reg_const_1, tile5, tile3);
                        // tile4 = v
                        maa_indirect_load<NodeID>(g.out_neighbors_, tile3, tile4);
                        wait_ready(tile5);
                        curr_tilej_size = get_tile_size(tile5);
                        if (curr_tilej_size == 0) {
                            break;
                        }
#pragma omp critical
                        {
                            // tile2 = old depths[v]
                            maa_indirect_load<int>(depths.data(), tile4, tile2);
                            // tile1 = if (old depths[v] == -1)
                            maa_alu_scalar<int>(tile2, reg_const_minus_one, tile1, Operation_t::EQ_OP);
                            // if (old depths[v] == -1): new depths[v] = depth, tile0 = inter depths[v]
                            maa_indirect_store_scalar<int>(depths.data(), tile4, regDepth, tile1, tile0);
                            wait_ready(tile0);
                        }
                        // Transfer tile4, tile5, tile1
                        // tile2 = new depths[v]
                        maa_indirect_load<int>(depths.data(), tile4, tile2);
                        wait_ready(tile2);
                        // #pragma omp critical
                        //                         std::cout << "core " << tid << " processing " << curr_tilej_size << " elements" << std::endl;
                        for (int j = 0; j < curr_tilej_size; j++) {
                            // if (old depths[v] == -1 && inter depths[v] == -1)
                            if (tile1_ptr[j] && tile0_ptr[j] == -1) {
                                lqueue.push_back(tile4_ptr[j]);
                            }
                            // if (new depths[v] == depth)
                            if (tile2_ptr[j] == depth) {
                                succ.set_bit_atomic(tile3_ptr[j]);
                            }
                        }
                        // tile1 = if (new depths[v] == depth)
                        maa_alu_scalar<int>(tile2, regDepth, tile1, Operation_t::EQ_OP);
                        // if (new depths[v] == depth): tile0 = u
                        maa_indirect_load<NodeID>(base + min, tile5, tile0, tile1);
                        // if (new depths[v] == depth): tile1 = path_counts[u]
                        maa_indirect_load<CountT>(path_counts.data(), tile0, tile2, tile1);
                        // if (new depths[v] == depth): path_counts[v] += path_counts[u]
                        maa_indirect_rmw_vector<CountT>(path_counts.data(), tile4, tile2, Operation_t::ADD_OP, tile1);
                        wait_ready(tile2);
                    } while (curr_tilej_size > 0);
                    idx += NUM_CORES * tile_size;
                } else { // else use base
#pragma omp barrier
// #pragma omp master
//                     std::cout << "Starting PBFSMAA (d: " << depth << "): " << len << " elements, idx: " << idx << " (base)" << std::endl;
#pragma omp for schedule(dynamic, 64) nowait
                    for (int i = idx; i < len; i++) {
                        NodeID u = base[i];
                        for (SGOffset vidx = VertexOffsetsData[u]; vidx < VertexOffsetsData[u + 1]; vidx++) {
                            NodeID &v = g.out_neighbors_[vidx];
                            if ((depths[v] == -1) && (compare_and_swap(depths[v], static_cast<NodeID>(-1), depth))) {
                                lqueue.push_back(v);
                            }
                            if (depths[v] == depth) {
                                succ.set_bit_atomic(vidx);
#pragma omp atomic
                                path_counts[v] += path_counts[u];
                            }
                        }
                    }
                    idx = len;
                }
            }
            // std::cout << lqueue.in << std::endl;
            lqueue.flush();
#pragma omp barrier
#pragma omp single
            {
                depth_index.push_back(queue.begin_idx());
                queue.slide_window();
            }
        }
    }
    depth_index.push_back(queue.begin_idx());
}

pvector<ScoreT> Brandes(const Graph &g, SourcePicker<Graph> &sp, NodeID num_iters, bool logging_enabled = false) {
    Timer t;
    t.Start();
    alloc_MAA();
    init_MAA();
    pvector<ScoreT> scores(g.num_nodes(), 0);
    pvector<CountT> path_counts(g.num_nodes());
    Bitmap succ(g.num_edges_directed());
    vector<SlidingQueue<NodeID>::iterator> depth_index;
    SlidingQueue<NodeID> queue(g.num_nodes());
    t.Stop();
    if (logging_enabled)
        PrintStep("a", t.Seconds());
    pvector<SGOffset> VertexOffsetsOut = g.VertexOffsets(false);
    SGOffset *VertexOffsetsData = VertexOffsetsOut.data();
    pvector<ScoreT> deltas(g.num_nodes());
    pvector<NodeID> depths(g.num_nodes());

#pragma omp parallel
    {
#ifdef GEM5
#pragma omp master
        {
            clear_mem_region();
            add_mem_region(queue.beginp(), queue.endp());                                // 6
            add_mem_region(VertexOffsetsOut.beginp(), VertexOffsetsOut.endp());          // 7
            add_mem_region(g.out_neighbors_, &g.out_neighbors_[g.num_edges_directed()]); // 8
            add_mem_region(succ.beginp(), succ.endp());                                  // 9
            add_mem_region(path_counts.beginp(), path_counts.endp());                    // 10
            add_mem_region(deltas.beginp(), deltas.endp());                              // 11
            add_mem_region(scores.beginp(), scores.endp());                              // 12
            add_mem_region(depths.beginp(), depths.endp());                              // 13
            std::cout << "ROI started: " << omp_get_num_threads() << " threads" << std::endl;
            m5_work_begin(0, 0);
            m5_reset_stats(0, 0);
        }
#endif
    }

    for (NodeID iter = 0; iter < num_iters; iter++) {
        NodeID source = sp.PickNext();
        if (logging_enabled)
            PrintStep("Source", static_cast<int64_t>(source));
        t.Start();
        path_counts.fill(0);
        deltas.fill(0);
        depth_index.resize(0);
        queue.reset();
        succ.reset();
        PBFS(g, VertexOffsetsData, source, path_counts, succ, depth_index, queue, depths);
        t.Stop();
        if (logging_enabled)
            PrintStep("b", t.Seconds());
        t.Start();
        for (int d = depth_index.size() - 2; d >= 0; d--) {
            std::cout << "Starting Brandes: " << d << "/" << depth_index.size() << ": " << *(depth_index[d]) << "-" << *(depth_index[d + 1]) << " elements" << std::endl;
#pragma omp parallel for schedule(dynamic, 64)
            for (auto it = depth_index[d]; it < depth_index[d + 1]; it++) {
                NodeID u = *it;
                ScoreT delta_u = 0;
                for (SGOffset vidx = VertexOffsetsData[u]; vidx < VertexOffsetsData[u + 1]; vidx++) {
                    NodeID v = g.out_neighbors_[vidx];
                    if (succ.get_bit(vidx)) {
                        delta_u += (path_counts[u] / path_counts[v]) * (1 + deltas[v]);
                    }
                }
                deltas[u] = delta_u;
                scores[u] += delta_u;
            }
        }
        t.Stop();
        if (logging_enabled)
            PrintStep("p", t.Seconds());
    }
    // normalize scores
    ScoreT biggest_score = 0;
#pragma omp parallel for reduction(max : biggest_score)
    for (NodeID n = 0; n < g.num_nodes(); n++)
        biggest_score = max(biggest_score, scores[n]);
#pragma omp parallel for
    for (NodeID n = 0; n < g.num_nodes(); n++)
        scores[n] = scores[n] / biggest_score;

#ifdef GEM5
    clear_mem_region();
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
    std::cout << "ROI End!!!" << std::endl;
    m5_exit(0);
#endif

    return scores;
}

pvector<ScoreT> BrandesMaa(const Graph &g, SourcePicker<Graph> &sp, NodeID num_iters, bool logging_enabled = false) {
    Timer t;
    t.Start();
    alloc_MAA();
    init_MAA();
    pvector<ScoreT> scores(g.num_nodes(), 0);
    pvector<CountT> path_counts(g.num_nodes());
    Bitmap succ(g.num_edges_directed());
    vector<int> depth_index;
    SlidingQueue<NodeID> queue(g.num_nodes());
    pvector<SGOffset> VertexOffsetsOut = g.VertexOffsets(false);
    SGOffset *VertexOffsetsData = VertexOffsetsOut.data();
    pvector<ScoreT> deltas(g.num_nodes());
    pvector<NodeID> depths(g.num_nodes());
    t.Stop();
    if (logging_enabled)
        PrintStep("a", t.Seconds());

#pragma omp parallel
    {
#ifdef GEM5
#pragma omp master
        {
            clear_mem_region();
            add_mem_region(queue.beginp(), queue.endp());                                // 6
            add_mem_region(VertexOffsetsOut.beginp(), VertexOffsetsOut.endp());          // 7
            add_mem_region(g.out_neighbors_, &g.out_neighbors_[g.num_edges_directed()]); // 8
            add_mem_region(succ.beginp(), succ.endp());                                  // 9
            add_mem_region(path_counts.beginp(), path_counts.endp());                    // 10
            add_mem_region(deltas.beginp(), deltas.endp());                              // 11
            add_mem_region(scores.beginp(), scores.endp());                              // 12
            add_mem_region(depths.beginp(), depths.endp());                              // 13
            std::cout << "ROI started: " << omp_get_num_threads() << " threads" << std::endl;
            m5_work_begin(0, 0);
            m5_reset_stats(0, 0);
        }
#endif
#pragma omp critical
        {
            int tid = omp_get_thread_num();
            tiles0[tid] = get_new_tile<int>();
            tiles1[tid] = get_new_tile<int>();
            tiles2[tid] = get_new_tile<int>();
            tiles3[tid] = get_new_tile<int>();
            tiles4[tid] = get_new_tile<int>();
            tiles5[tid] = get_new_tile<int>();
            tiles6[tid] = get_new_tile<int>();
            tiles7[tid] = get_new_tile<int>();
            regs0[tid] = get_new_reg<int>();
            regs1[tid] = get_new_reg<int>();
            regs2[tid] = get_new_reg<int>();
            regs3[tid] = get_new_reg<int>();
            regs4[tid] = get_new_reg<int>();
            regs5[tid] = get_new_reg<int>();
            regs6[tid] = get_new_reg<int>();
            regs7[tid] = get_new_reg<int>();
        }
    }

    for (NodeID iter = 0; iter < num_iters; iter++) {
        NodeID source = sp.PickNext();
        if (logging_enabled)
            PrintStep("Source", static_cast<int64_t>(source));
        t.Start();
        path_counts.fill(0);
        deltas.fill(0);
        depth_index.resize(0);
        queue.reset();
        succ.reset();
        PBFSMAA(g, VertexOffsetsData, source, path_counts, succ, depth_index, queue, depths);
        t.Stop();
        if (logging_enabled)
            PrintStep("b", t.Seconds());
        t.Start();
#pragma omp parallel
        {
            int tilev, tileu, tile_ub_d, tile_lb_d, tilei, tilej;
            int reg0, reg1, regOne, last_i_reg, last_j_reg;
            int tid = omp_get_thread_num();
            tilev = tiles0[tid];
            tileu = tiles1[tid];
            tilej = tiles2[tid];
            tile_lb_d = tiles3[tid];
            tile_ub_d = tiles4[tid];
            tilei = tiles5[tid];
            reg0 = regs0[tid];
            reg1 = regs1[tid];
            regOne = regs2[tid];
            last_i_reg = regs6[tid];
            last_j_reg = regs7[tid];
            maa_const<int>(1, regOne);
            uint32_t *u_ptr = get_cacheable_tile_pointer<uint32_t>(tileu);
            uint32_t *v_ptr = get_cacheable_tile_pointer<uint32_t>(tilev);
            uint32_t *j_ptr = get_cacheable_tile_pointer<uint32_t>(tilej);
            for (int d = depth_index.size() - 2; d >= 0; d--) {
                int len = depth_index[d + 1];
                int idx = depth_index[d];
                while (idx < len) {
                    int current_len_size = len - idx;
#if TILE_SIZE == 32768
                    const int tile_size = current_len_size > NUM_CORES * 32768   ? 32768
                                          : current_len_size > NUM_CORES * 16384 ? 16384
                                          : current_len_size > NUM_CORES * 8192  ? 8192
                                          : current_len_size > NUM_CORES * 4096  ? 4096
                                          : current_len_size > NUM_CORES * 2048  ? 2048
                                          : current_len_size > NUM_CORES * 1024  ? 1024
                                                                                 : -1;
#elif TILE_SIZE == 16384
                    const int tile_size = current_len_size > NUM_CORES * 16384  ? 16384
                                          : current_len_size > NUM_CORES * 8192 ? 8192
                                          : current_len_size > NUM_CORES * 4096 ? 4096
                                          : current_len_size > NUM_CORES * 2048 ? 2048
                                          : current_len_size > NUM_CORES * 1024 ? 1024
                                                                                : -1;
#elif TILE_SIZE == 8192
                    const int tile_size = current_len_size > NUM_CORES * 8192   ? 8192
                                          : current_len_size > NUM_CORES * 4096 ? 4096
                                          : current_len_size > NUM_CORES * 2048 ? 2048
                                          : current_len_size > NUM_CORES * 1024 ? 1024
                                                                                : -1;
#elif TILE_SIZE == 4096
                    const int tile_size = current_len_size > NUM_CORES * 4096   ? 4096
                                          : current_len_size > NUM_CORES * 2048 ? 2048
                                          : current_len_size > NUM_CORES * 1024 ? 1024
                                                                                : -1;
#elif TILE_SIZE == 2048
                    const int tile_size = current_len_size > NUM_CORES * 2048   ? 2048
                                          : current_len_size > NUM_CORES * 1024 ? 1024
                                                                                : -1;
#elif TILE_SIZE == 1024
                    const int tile_size = current_len_size > NUM_CORES * 1024 ? 1024
                                                                              : -1;
#else
                    assert(false);
#endif
                    if (tile_size != -1) {
                        // #pragma omp master
                        //                         std::cout << "Starting BrandesMaa: " << d << "/" << depth_index.size() - 2 << ": " << depth_index[d] << "-" << depth_index[d + 1] << " elements, tile: " << tile_size << " idx: " << idx << " (maa)" << std::endl;
                        int min = idx + tid * tile_size;
                        int max = min + tile_size;
                        maa_const<int>(min, reg0);
                        maa_const<int>(max, reg1);
                        // step1: streamingly load u
                        maa_stream_load<int>(queue.shared, reg0, reg1, regOne, tileu);
                        // step2: load upper bounds of VertexOffsetsData using u
                        maa_indirect_load<SGOffset>(VertexOffsetsData, tileu, tile_lb_d);
                        maa_indirect_load<SGOffset>(&VertexOffsetsData[1], tileu, tile_ub_d);
                        // step3 do while loop
                        int curr_tilej_size = 0;
                        maa_const<int>(0, last_i_reg);
                        maa_const<int>(-1, last_j_reg);
                        do {
                            maa_range_loop<int>(last_i_reg, last_j_reg, tile_lb_d, tile_ub_d, regOne, tilei, tilej);
                            //  NodeID v = g.out_neighbors_[vidx];
                            maa_indirect_load<NodeID>(g.out_neighbors_, tilej, tilev);
                            // Transfer tilei
                            // load path u using itile: first indirect load u
                            maa_indirect_load<int>((NodeID *)(queue.shared + min), tilei, tileu);
                            wait_ready(tilej);
                            curr_tilej_size = get_tile_size(tilej);
                            wait_ready(tilev);
                            wait_ready(tileu);
#pragma omp simd aligned(j_ptr, v_ptr, u_ptr : 16) simdlen(4)
                            for (int i = 0; i < curr_tilej_size; i++) {
                                if (succ.get_bit(j_ptr[i])) {
                                    uint32_t v = v_ptr[i];
                                    uint32_t u = u_ptr[i];
                                    ScoreT tmp = (path_counts[u] / path_counts[v]) * (1 + deltas[v]);
                                    deltas[u] += tmp;
                                    scores[u] += tmp;
                                }
                            }
                        } while (curr_tilej_size > 0);
                        idx += NUM_CORES * tile_size;
                    } else {
#pragma omp barrier
#pragma omp for schedule(dynamic, 64)
                        for (int it = idx; it < len; it++) {
                            NodeID u = queue.shared[it];
                            ScoreT delta_u = 0;
                            for (SGOffset vidx = VertexOffsetsData[u]; vidx < VertexOffsetsData[u + 1]; vidx++) {
                                NodeID v = g.out_neighbors_[vidx];
                                if (succ.get_bit(vidx)) {
                                    delta_u += (path_counts[u] / path_counts[v]) * (1 + deltas[v]);
                                }
                            }
                            deltas[u] = delta_u;
                            scores[u] += delta_u;
                        }
                        idx = len;
                    }
                }
            } // outter iteration loop
        }
        t.Stop();
        if (logging_enabled)
            PrintStep("p", t.Seconds());
    }
    // normalize scores
    ScoreT biggest_score = 0;
#pragma omp parallel for reduction(max : biggest_score)
    for (NodeID n = 0; n < g.num_nodes(); n++)
        biggest_score = max(biggest_score, scores[n]);
#pragma omp parallel for
    for (NodeID n = 0; n < g.num_nodes(); n++)
        scores[n] = scores[n] / biggest_score;

#ifdef GEM5
    clear_mem_region();
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
    m5_exit(0);
    std::cout << "ROI End!!!" << std::endl;
#endif

    return scores;
}

void PrintTopScores(const Graph &g, const pvector<ScoreT> &scores) {
    vector<pair<NodeID, ScoreT>> score_pairs(g.num_nodes());
    for (NodeID n : g.vertices())
        score_pairs[n] = make_pair(n, scores[n]);
    int k = 5;
    vector<pair<ScoreT, NodeID>> top_k = TopK(score_pairs, k);
    for (auto kvp : top_k)
        cout << kvp.second << ":" << kvp.first << endl;
}

// Still uses Brandes algorithm, but has the following differences:
// - serial (no need for atomics or dynamic scheduling)
// - uses vector for BFS queue
// - regenerates farthest to closest traversal order from depths
// - regenerates successors from depths
bool BCVerifier(const Graph &g, SourcePicker<Graph> &sp, NodeID num_iters,
                const pvector<ScoreT> &scores_to_test) {
    pvector<ScoreT> scores(g.num_nodes(), 0);
    for (int iter = 0; iter < num_iters; iter++) {
        NodeID source = sp.PickNext();
        // BFS phase, only records depth & path_counts
        pvector<int> depths(g.num_nodes(), -1);
        depths[source] = 0;
        vector<CountT> path_counts(g.num_nodes(), 0);
        path_counts[source] = 1;
        vector<NodeID> to_visit;
        to_visit.reserve(g.num_nodes());
        to_visit.push_back(source);
        for (auto it = to_visit.begin(); it != to_visit.end(); it++) {
            NodeID u = *it;
            for (NodeID v : g.out_neigh(u)) {
                if (depths[v] == -1) {
                    depths[v] = depths[u] + 1;
                    to_visit.push_back(v);
                }
                if (depths[v] == depths[u] + 1)
                    path_counts[v] += path_counts[u];
            }
        }
        // Get lists of vertices at each depth
        vector<vector<NodeID>> verts_at_depth;
        for (NodeID n : g.vertices()) {
            if (depths[n] != -1) {
                if (depths[n] >= static_cast<int>(verts_at_depth.size()))
                    verts_at_depth.resize(depths[n] + 1);
                verts_at_depth[depths[n]].push_back(n);
            }
        }
        // Going from farthest to closest, compute "dependencies" (deltas)
        pvector<ScoreT> deltas(g.num_nodes(), 0);
        for (int depth = verts_at_depth.size() - 1; depth >= 0; depth--) {
            for (NodeID u : verts_at_depth[depth]) {
                for (NodeID v : g.out_neigh(u)) {
                    if (depths[v] == depths[u] + 1) {
                        deltas[u] += (path_counts[u] / path_counts[v]) * (1 + deltas[v]);
                    }
                }
                scores[u] += deltas[u];
            }
        }
    }
    // Normalize scores
    ScoreT biggest_score = *max_element(scores.begin(), scores.end());
    for (NodeID n : g.vertices())
        scores[n] = scores[n] / biggest_score;
    // Compare scores
    bool all_ok = true;
    for (NodeID n : g.vertices()) {
        ScoreT delta = abs(scores_to_test[n] - scores[n]);
        if (delta > std::numeric_limits<ScoreT>::epsilon()) {
            cout << n << ": " << scores[n] << " != " << scores_to_test[n];
            cout << "(" << delta << ")" << endl;
            all_ok = false;
        }
    }
    return all_ok;
}

int main(int argc, char *argv[]) {
    CLIterApp cli(argc, argv, "betweenness-centrality", 1);
    if (!cli.ParseArgs())
        return -1;
    if (cli.num_iters() > 1 && cli.start_vertex() != -1)
        cout << "Warning: iterating from same source (-r & -i)" << endl;
    Builder b(cli);
    Graph g = b.MakeGraph();
    SourcePicker<Graph> sp(g, cli.start_vertex());
#ifdef GEM5
    std::cout << "Fake Checkpoint started" << std::endl;
    m5_checkpoint(0, 0);
    std::cout << "Fake Checkpoint ended" << std::endl;
#endif
    auto BCBound = [&sp, &cli](const Graph &g) {
#ifdef MAA
        return BrandesMaa(g, sp, cli.num_iters(), cli.logging_en());
#else
        return Brandes(g, sp, cli.num_iters(), cli.logging_en());
#endif
    };
    SourcePicker<Graph> vsp(g, cli.start_vertex());
    auto VerifierBound = [&vsp, &cli](const Graph &g,
                                      const pvector<ScoreT> &scores) {
        return BCVerifier(g, vsp, cli.num_iters(), scores);
    };
    BenchmarkKernel(cli, g, BCBound, PrintTopScores, VerifierBound);
    return 0;
}
