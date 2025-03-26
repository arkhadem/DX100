// Copyright (c) 2015, The Regents of the University of California (Regents)
// See LICENSE.txt for license details

#include <functional>
#include <iostream>
#include <vector>
#include <omp.h>

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

#if !defined(FUNC) && !defined(GEM5) && !defined(GEM5_MAGIC)
#define FUNC
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

#define MAX_THREAD_NUM 4
int tiles0[MAX_THREAD_NUM], tiles1[MAX_THREAD_NUM], tiles2[MAX_THREAD_NUM], tiles3[MAX_THREAD_NUM], tiles4[MAX_THREAD_NUM], tiles5[MAX_THREAD_NUM], tiles6[MAX_THREAD_NUM], tiles7[MAX_THREAD_NUM], tiles8[MAX_THREAD_NUM], tilesi[MAX_THREAD_NUM], tilesj[MAX_THREAD_NUM];
int regs0[MAX_THREAD_NUM], regs1[MAX_THREAD_NUM], regs2[MAX_THREAD_NUM], regs3[MAX_THREAD_NUM], regs4[MAX_THREAD_NUM], regs5[MAX_THREAD_NUM], last_i_regs[MAX_THREAD_NUM], last_j_regs[MAX_THREAD_NUM];

void PBFS(const Graph &g, NodeID source, pvector<CountT> &path_counts,
          Bitmap &succ, vector<SlidingQueue<NodeID>::iterator> &depth_index,
          SlidingQueue<NodeID> &queue) {
    pvector<NodeID> depths(g.num_nodes(), -1);
    depths[source] = 0;
    path_counts[source] = 1;
    queue.push_back(source);
    depth_index.push_back(queue.begin());
    queue.slide_window();
    const NodeID *g_out_start = g.out_neigh(0).begin();
    pvector<SGOffset> VertexOffsetsOut = g.VertexOffsets(false);
    SGOffset *VertexOffsetsData = VertexOffsetsOut.data();
#pragma omp parallel
    {
        NodeID depth = 0;
        QueueBuffer<NodeID> lqueue(queue);
        while (!queue.empty()) {
            depth++;
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

void PBFSMAA(const Graph &g, NodeID source, pvector<CountT> &path_counts,
             Bitmap &succ, vector<int> &depth_index,
             SlidingQueue<NodeID> &queue) {
    pvector<NodeID> depths(g.num_nodes(), -1);
    depths[source] = 0;
    path_counts[source] = 1;
    queue.push_back(source);
    depth_index.push_back(queue.begin_idx());
    queue.slide_window();
// const NodeID* g_out_start = g.out_neigh(0).begin();
#pragma omp parallel
    {
        int tile0, tile1, tile3, tile4, tile5, tile6, tilei, tilej;
        int reg0, reg1, reg2, reg3, reg4, reg5, last_i_reg, last_j_reg;
        // reg0 used as 1
        // reg1 used as -1
        // reg3 used as 2
        int thread_id = omp_get_thread_num();
        tile0 = tiles0[thread_id];
        tile1 = tiles1[thread_id];
        tile3 = tiles3[thread_id];
        tile4 = tiles4[thread_id];
        tile5 = tiles5[thread_id];
        tile6 = tiles6[thread_id];
        tilei = tilesi[thread_id];
        tilej = tilesj[thread_id];
        reg0 = regs0[thread_id];
        reg1 = regs1[thread_id];
        reg2 = regs2[thread_id];
        reg3 = regs3[thread_id];
        reg4 = regs4[thread_id];
        reg5 = regs5[thread_id];
        last_i_reg = last_i_regs[thread_id];
        last_j_reg = last_j_regs[thread_id];
        NodeID depth = 0;
        QueueBuffer<NodeID> lqueue(queue);
        pvector<SGOffset> VertexOffsetsOut = g.VertexOffsets(false);
        SGOffset *VertexOffsetsData = VertexOffsetsOut.data();
        // NodeID** out_index = g.out_index_;
        /*
    while (!queue.empty()) {
      depth++;
      NodeID* base = queue.shared + queue.shared_out_start;
      int len = queue.size();
      for (int i = 0; i < len; i += TILE_SIZE){
        #pragma omp for schedule(dynamic, 64) nowait
        for (int j = i; j < min(len, i+TILE_SIZE); j++) { 
          NodeID u = base[j];
          for (NodeID *v_idx = out_index[u]; v_idx < out_index[u+1]; v_idx++) {
            NodeID& v = *v_idx;
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
        } // iteration
      } // TILING
      lqueue.flush();
      #pragma omp barrier
      #pragma omp single
      {
        depth_index.push_back(queue.begin());
        queue.slide_window();
      }
    }
    
    */

        while (!queue.empty()) {
            depth++;
            NodeID *base = queue.shared + queue.shared_out_start;
            int len = queue.size();
            maa_const<int>(len, reg3);
#pragma omp for schedule(dynamic, 64) nowait
            for (int idx = 0; idx < len; idx += TILE_SIZE) {
                NodeID *udata = get_cacheable_tile_pointer<NodeID>(tile0);
                NodeID *vdata = get_cacheable_tile_pointer<NodeID>(tile6);
                SGOffset *vaddrOffset = get_cacheable_tile_pointer<SGOffset>(tilej);
                int *idata = get_cacheable_tile_pointer<int>(tilei);
                maa_const<int>(idx, reg2);

                maa_stream_load<NodeID>(base, reg2, reg3, reg0, tile0);
                // tile1,2 would hold out_index[u]
                maa_indirect_load<SGOffset>(VertexOffsetsData, tile0, tile1);
                // tile3,4 would hold out_index[u]
                maa_indirect_load<SGOffset>(&VertexOffsetsData[1], tile0, tile3);
                int curr_tilej_size = 0;
                maa_const<int>(0, last_i_reg);
                maa_const<SGOffset>(-1, last_j_reg);
                do {
                    maa_range_loop<SGOffset>(last_i_reg, last_j_reg, tile1, tile3, reg0, tilei, tilej);
                    // load v
                    maa_indirect_load<NodeID>(g.out_neighbors_, tilej, tile6);
                    // load depths[v]
                    wait_ready(tilej);
                    curr_tilej_size = get_tile_size(tilej);
                    for (int j = 0; j < curr_tilej_size; j++) {
                        NodeID &v = vdata[j];
                        NodeID &u = udata[idata[j]];
                        // std::cout << "u: " << u << " v: " << v << std::endl;
                        SGOffset voffset = vaddrOffset[j];
                        if ((depths[v] == -1) &&
                            (compare_and_swap(depths[v], static_cast<NodeID>(-1), depth))) {
                            lqueue.push_back(v);
                        }
                        if (depths[v] == depth) {
                            succ.set_bit_atomic(voffset);
#pragma omp atomic
                            path_counts[v] += path_counts[u];
                        }
                    }
                } while (curr_tilej_size > 0);
            } // TILING
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

pvector<ScoreT> Brandes(const Graph &g, SourcePicker<Graph> &sp,
                        NodeID num_iters, bool logging_enabled = false) {
    Timer t;
    t.Start();
    pvector<ScoreT> scores(g.num_nodes(), 0);
    pvector<CountT> path_counts(g.num_nodes());
    Bitmap succ(g.num_edges_directed());
    vector<SlidingQueue<NodeID>::iterator> depth_index;
    SlidingQueue<NodeID> queue(g.num_nodes());
    t.Stop();
    if (logging_enabled)
        PrintStep("a", t.Seconds());
    const NodeID *g_out_start = g.out_neigh(0).begin();
    for (NodeID iter = 0; iter < num_iters; iter++) {
        NodeID source = sp.PickNext();
        if (logging_enabled)
            PrintStep("Source", static_cast<int64_t>(source));
        t.Start();
        path_counts.fill(0);
        depth_index.resize(0);
        queue.reset();
        succ.reset();
        PBFS(g, source, path_counts, succ, depth_index, queue);
        t.Stop();
        if (logging_enabled)
            PrintStep("b", t.Seconds());
        pvector<ScoreT> deltas(g.num_nodes(), 0);
        t.Start();
        for (int d = depth_index.size() - 2; d >= 0; d--) {
#pragma omp parallel for schedule(dynamic, 64)
            for (auto it = depth_index[d]; it < depth_index[d + 1]; it++) {
                NodeID u = *it;
                ScoreT delta_u = 0;
                for (NodeID &v : g.out_neigh(u)) {
                    if (succ.get_bit(&v - g_out_start)) {
                        ScoreT tmp = (path_counts[u] / path_counts[v]) * (1 + deltas[v]);
                        deltas[u] += tmp;
                        scores[u] += tmp;
                    }
                }
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
    return scores;
}

pvector<ScoreT> BrandesMaa2(const Graph &g, SourcePicker<Graph> &sp,
                            NodeID num_iters, bool logging_enabled = false) {
    Timer t;
    t.Start();
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
            tiles6[thread_id] = get_new_tile<int>();
            tiles7[thread_id] = get_new_tile<int>();
            tiles8[thread_id] = get_new_tile<int>();
            regs0[thread_id] = get_new_reg<int>(1);
            regs1[thread_id] = get_new_reg<int>(-1);
            regs2[thread_id] = get_new_reg<int>();
            regs3[thread_id] = get_new_reg<int>();
            regs4[thread_id] = get_new_reg<int>();
            regs5[thread_id] = get_new_reg<int>();
            last_i_regs[thread_id] = get_new_reg<int>();
            last_j_regs[thread_id] = get_new_reg<int>();
        }
    }

    pvector<ScoreT> scores(g.num_nodes(), 0);
    pvector<CountT> path_counts(g.num_nodes());
    Bitmap succ(g.num_edges_directed());
    vector<int> depth_index;
    SlidingQueue<NodeID> queue(g.num_nodes());
    pvector<SGOffset> VertexOffsetsOut = g.VertexOffsets(false);
    SGOffset *VertexOffsetsData = VertexOffsetsOut.data();
    t.Stop();
    if (logging_enabled)
        PrintStep("a", t.Seconds());
    for (NodeID iter = 0; iter < num_iters; iter++) {
        NodeID source = sp.PickNext();
        if (logging_enabled)
            PrintStep("Source", static_cast<int64_t>(source));
        t.Start();
        path_counts.fill(0);
        depth_index.resize(0);
        queue.reset();
        succ.reset();
        PBFSMAA(g, source, path_counts, succ, depth_index, queue);
        CountT *path_counts_data = path_counts.data();
        t.Stop();
        if (logging_enabled)
            PrintStep("b", t.Seconds());
        pvector<ScoreT> deltas(g.num_nodes(), 0);
        t.Start();
        for (int d = depth_index.size() - 2; d >= 0; d--) {
#pragma omp parallel
            {
                int tile0, tileu, tile3, tile_ub_d, tile_lb_d, tile_cond, tilei, tilej, tile_dalta_v, tile_path_counts;
                int reg0, reg1, regOne, reg3, reg4, reg5, last_i_reg, last_j_reg;
                int thread_id = omp_get_thread_num();
                tile0 = tiles0[thread_id];
                tileu = tiles1[thread_id];
                tile3 = tiles3[thread_id];
                tile_lb_d = tiles8[thread_id];
                tile_ub_d = tiles4[thread_id];
                tilei = tilesi[thread_id];
                tilej = tilesj[thread_id];
                tile_cond = tiles6[thread_id];
                tile_dalta_v = tiles5[thread_id];
                tile_path_counts = tiles7[thread_id];
                reg0 = regs0[thread_id];
                reg1 = regs1[thread_id];
                regOne = regs2[thread_id];
                reg3 = regs3[thread_id];
                reg4 = regs4[thread_id];
                reg5 = regs5[thread_id];
                last_i_reg = last_i_regs[thread_id];
                last_j_reg = last_j_regs[thread_id];
                maa_const<int>(depth_index[d + 1], reg1);
                maa_const<int>(1, regOne);
                maa_const<float>(1.0, reg5);
                maa_const(5, reg3);
                maa_const(31, reg4);
#pragma omp for schedule(dynamic, 64)
                for (int it = depth_index[d]; it < depth_index[d + 1]; it += TILE_SIZE) {
                    maa_const<int>(it, reg0);
                    // step1: streamingly load u
                    maa_stream_load<int>(queue.shared, reg0, reg1, regOne, tile0);
                    // step2: load upper bounds of VertexOffsetsData using u
                    maa_indirect_load<SGOffset>(VertexOffsetsData, tile0, tile_lb_d);
                    maa_indirect_load<SGOffset>(&VertexOffsetsData[1], tile0, tile_ub_d);
                    assert(get_tile_size(tile_lb_d) == get_tile_size(tile_ub_d));

                    // step3 do while loop
                    int curr_tilej_size = 0;
                    maa_const<int>(0, last_i_reg);
                    maa_const<int>(-1, last_j_reg);

                    do {
                        maa_range_loop<int>(last_i_reg, last_j_reg, tile_lb_d, tile_ub_d, regOne, tilei, tilej);
                        wait_ready(tilej);
                        curr_tilej_size = get_tile_size(tilej);
                        // uint32_t* ptr = get_cacheable_tile_pointer<uint32_t>(tile3);
                        // if (succ.get_bit(vidx)):
                        // tile3: vidx >> 5
                        // tile0: succ.start_[tile3]
                        // tile_cond: vidx & 31
                        // tile3: tile0 >> tile_cond
                        // tile_cond: tile3 & 1
                        maa_alu_scalar<uint32_t>(tilej, reg3, tile3, Operation_t::SHR_OP);
                        maa_indirect_load<uint32_t>(succ.start_, tile3, tile0);
                        maa_alu_scalar<uint32_t>(tilej, reg4, tile_cond, Operation_t::AND_OP);
                        maa_alu_vector<uint32_t>(tile0, tile_cond, tile3, Operation_t::SHR_OP);
                        maa_alu_scalar<uint32_t>(tile3, regOne, tile_cond, Operation_t::AND_OP);
                        //  NodeID v = g.out_neighbors_[vidx];
                        maa_indirect_load<NodeID>(g.out_neighbors_, tilej, tile0);
                        // load path u using itile: first indirect load u
                        maa_indirect_load<int>((NodeID *)(queue.shared + it), tilei, tileu);

                        // // if (succ.get_bit(vidx)) {
                        // // first conditionally load deltas[v]
#ifdef MAA_FULL
                        maa_indirect_load<float>(deltas.data(), tile0, tile3, tile_cond);
                        maa_alu_scalar<float>(tile3, reg5, tile_dalta_v, Operation_t::ADD_OP, tile_cond);
                        // then load path v
                        maa_indirect_load<CountT>(path_counts_data, tile0, tile3, tile_cond);
                        maa_indirect_load<CountT>(path_counts_data, tileu, tilei, tile_cond);
                        // perform division of path_counts[u] / path_counts[v]
                        maa_alu_vector<CountT>(tilei, tile3, tile_path_counts, Operation_t::DIV_OP, tile_cond);
                        // multiply by (1 + deltas[v])
                        maa_alu_vector<float>(tile_path_counts, tile_dalta_v, tile3, Operation_t::MUL_OP, tile_cond);
#else
                        // detla v
                        maa_indirect_load<float>(deltas.data(), tile0, tile_dalta_v, tile_cond);
                        // path_counts_data[u]
                        maa_indirect_load<float>(path_counts_data, tileu, tilei, tile_cond);
                        // path_counts_data[v]
                        maa_indirect_load<float>(path_counts_data, tile0, tile_path_counts, tile_cond);

                        __restrict__ uint32_t *tile_cond_ptr = get_cacheable_tile_pointer<uint32_t>(tile_cond);
                        float *tile3_ptr = get_cacheable_tile_pointer<float>(tile3);
                        float *tile_dalta_v_ptr = get_cacheable_tile_pointer<float>(tile_dalta_v);
                        float *path_counts_u_ptr = get_cacheable_tile_pointer<float>(tilei);
                        float *path_counts_v_ptr = get_cacheable_tile_pointer<float>(tile_path_counts);

#pragma omp simd aligned(tile_cond_ptr, tile3_ptr, path_counts_u_ptr, path_counts_v_ptr, tile_dalta_v_ptr : 16) simdlen(4)
                        for (int i = 0; i < get_tile_size(tile_cond); i++) {
                            if (tile_cond_ptr[i]) {
                                tile3_ptr[i] = (path_counts_u_ptr[i] / path_counts_v_ptr[i]) * (1 + tile_dalta_v_ptr[i]);
                            }
                        }
#endif
                        // store to deltas[u]
                        maa_indirect_rmw<float>(deltas.data(), tileu, tile3, Operation_t::ADD_OP, tile_cond);
                        maa_indirect_rmw<float>(scores.data(), tileu, tile3, Operation_t::ADD_OP, tile_cond);

                    } while (curr_tilej_size > 0);
                    // for (int i = it; i < min(depth_index[d+1], it+TILE_SIZE); i++) {
                    //   NodeID u = queue.shared[i];
                    //   ScoreT delta_u = 0;
                    //   for (SGOffset vidx = VertexOffsetsData[u]; vidx < VertexOffsetsData[u+1]; vidx++) {
                    //     NodeID v = g.out_neighbors_[vidx];
                    //     if (succ.get_bit(vidx)) {
                    //       delta_u += (path_counts[u] / path_counts[v]) * (1 + deltas[v]);
                    //     }
                    //   }
                    //   deltas[u] = delta_u;
                    //   scores[u] += delta_u;
                    // } // intra-tile loop
                } // inter-tile loop
            } // parallel region
        } // outter iteration loop
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
    return scores;
}

pvector<ScoreT> BrandesMaa(const Graph &g, SourcePicker<Graph> &sp,
                           NodeID num_iters, bool logging_enabled = false) {
    Timer t;
    t.Start();
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
            tiles6[thread_id] = get_new_tile<int>();
            tiles7[thread_id] = get_new_tile<int>();
            tiles8[thread_id] = get_new_tile<int>();
            regs0[thread_id] = get_new_reg<int>(1);
            regs1[thread_id] = get_new_reg<int>(-1);
            regs2[thread_id] = get_new_reg<int>();
            regs3[thread_id] = get_new_reg<int>();
            regs4[thread_id] = get_new_reg<int>();
            regs5[thread_id] = get_new_reg<int>();
            last_i_regs[thread_id] = get_new_reg<int>();
            last_j_regs[thread_id] = get_new_reg<int>();
        }
    }

    pvector<ScoreT> scores(g.num_nodes(), 0);
    pvector<CountT> path_counts(g.num_nodes());
    Bitmap succ(g.num_edges_directed());
    vector<int> depth_index;
    SlidingQueue<NodeID> queue(g.num_nodes());
    pvector<SGOffset> VertexOffsetsOut = g.VertexOffsets(false);
    SGOffset *VertexOffsetsData = VertexOffsetsOut.data();
    t.Stop();
    if (logging_enabled)
        PrintStep("a", t.Seconds());
    for (NodeID iter = 0; iter < num_iters; iter++) {
        NodeID source = sp.PickNext();
        if (logging_enabled)
            PrintStep("Source", static_cast<int64_t>(source));
        t.Start();
        path_counts.fill(0);
        depth_index.resize(0);
        queue.reset();
        succ.reset();
        PBFSMAA(g, source, path_counts, succ, depth_index, queue);
        CountT *path_counts_data = path_counts.data();
        t.Stop();
        if (logging_enabled)
            PrintStep("b", t.Seconds());
        pvector<ScoreT> deltas(g.num_nodes(), 0);
        t.Start();
        for (int d = depth_index.size() - 2; d >= 0; d--) {
#pragma omp parallel
            {
                int tilev, tileu, tile_ub_d, tile_lb_d, tilei, tilej;
                int reg0, reg1, regOne, reg3, reg4, reg5, last_i_reg, last_j_reg;
                int thread_id = omp_get_thread_num();
                tilev = tiles0[thread_id];
                tileu = tiles1[thread_id];
                tile_lb_d = tiles2[thread_id];
                tile_ub_d = tiles3[thread_id];
                tilei = tilesi[thread_id];
                tilej = tilesj[thread_id];
                reg0 = regs0[thread_id];
                reg1 = regs1[thread_id];
                regOne = regs2[thread_id];
                reg3 = regs3[thread_id];
                reg4 = regs4[thread_id];
                reg5 = regs5[thread_id];
                last_i_reg = last_i_regs[thread_id];
                last_j_reg = last_j_regs[thread_id];
                maa_const<int>(depth_index[d + 1], reg1);
                maa_const<int>(1, regOne);
                maa_const<float>(1.0, reg5);
                maa_const(5, reg3);
                maa_const(31, reg4);
#pragma omp for schedule(dynamic, 64)
                for (int it = depth_index[d]; it < depth_index[d + 1]; it += TILE_SIZE) {
                    maa_const<int>(it, reg0);
                    // step1: streamingly load u
                    maa_stream_load<int>(queue.shared, reg0, reg1, regOne, tileu);
                    // step2: load upper bounds of VertexOffsetsData using u
                    maa_indirect_load<SGOffset>(VertexOffsetsData, tileu, tile_lb_d);
                    maa_indirect_load<SGOffset>(&VertexOffsetsData[1], tileu, tile_ub_d);
                    assert(get_tile_size(tile_lb_d) == get_tile_size(tile_ub_d));

                    // step3 do while loop
                    int curr_tilej_size = 0;
                    maa_const<int>(0, last_i_reg);
                    maa_const<int>(-1, last_j_reg);

                    do {
                        maa_range_loop<int>(last_i_reg, last_j_reg, tile_lb_d, tile_ub_d, regOne, tilei, tilej);
                        //  NodeID v = g.out_neighbors_[vidx];
                        maa_indirect_load<NodeID>(g.out_neighbors_, tilej, tilev);
                        // load path u using itile: first indirect load u
                        maa_indirect_load<int>((NodeID *)(queue.shared + it), tilei, tileu);

                        uint32_t *u_ptr = get_cacheable_tile_pointer<uint32_t>(tileu);
                        uint32_t *v_ptr = get_cacheable_tile_pointer<uint32_t>(tilev);
                        uint32_t *j_ptr = get_cacheable_tile_pointer<uint32_t>(tilej);
                        wait_ready(tileu);
                        curr_tilej_size = get_tile_size(tilej);

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
                    // for (int i = it; i < min(depth_index[d+1], it+TILE_SIZE); i++) {
                    //   NodeID u = queue.shared[i];
                    //   ScoreT delta_u = 0;
                    //   for (SGOffset vidx = VertexOffsetsData[u]; vidx < VertexOffsetsData[u+1]; vidx++) {
                    //     NodeID v = g.out_neighbors_[vidx];
                    //     if (succ.get_bit(vidx)) {
                    //       delta_u += (path_counts[u] / path_counts[v]) * (1 + deltas[v]);
                    //     }
                    //   }
                    //   deltas[u] = delta_u;
                    //   scores[u] += delta_u;
                    // } // intra-tile loop
                } // inter-tile loop
            } // parallel region
        } // outter iteration loop
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

#else

void PBFSMAA(const Graph &g, NodeID source, pvector<CountT> &path_counts,
             Bitmap &succ, vector<int> &depth_index,
             SlidingQueue<NodeID> &queue) {
    pvector<NodeID> depths(g.num_nodes(), -1);
    depths[source] = 0;
    path_counts[source] = 1;
    queue.push_back(source);
    depth_index.push_back(queue.begin_idx());
    queue.slide_window();
    init_MAA();
    // set thread number to be 2
    // omp_set_num_threads(3);
    cout << "num_threads: " << omp_get_max_threads() << endl;
#pragma omp parallel
    {
        int tilelb, tile0, tileub, tile1, tilev, tilei, tilej, tile_cond;
        int reg0, reg1, reg2, regMax, regDepth, last_i_reg, last_j_reg;
        // reg0 used as 1
        // reg1 used as -1
        // reg3 used as 2

#pragma omp critical
        {
            tilelb = get_new_tile<int>();
            tile0 = get_new_tile<int>();
            tileub = get_new_tile<int>();
            tile1 = get_new_tile<int>();
            tilev = get_new_tile<int>();
            tilei = get_new_tile<int>();
            tilej = get_new_tile<int>();
            tile_cond = get_new_tile<int>();
            reg0 = get_new_reg<int>(1);
            reg1 = get_new_reg<int>(-1);
            reg2 = get_new_reg<int>();
            regMax = get_new_reg<int>();
            regDepth = get_new_reg<int>();
            last_i_reg = get_new_reg<int>();
            last_j_reg = get_new_reg<int>();
        }
        NodeID depth = 0;
        QueueBuffer<NodeID> lqueue(queue);
        pvector<SGOffset> VertexOffsetsOut = g.VertexOffsets(false);
        SGOffset *VertexOffsetsData = VertexOffsetsOut.data();
        // NodeID** out_index = g.out_index_;
        /*
    while (!queue.empty()) {
      depth++;
      NodeID* base = queue.shared + queue.shared_out_start;
      int len = queue.size();
      for (int i = 0; i < len; i += TILE_SIZE){
        #pragma omp for schedule(dynamic, 64) nowait
        for (int j = i; j < min(len, i+TILE_SIZE); j++) { 
          NodeID u = base[j];
          for (NodeID *v_idx = out_index[u]; v_idx < out_index[u+1]; v_idx++) {
            NodeID& v = *v_idx;
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
        } // iteration
      } // TILING
      lqueue.flush();
      #pragma omp barrier
      #pragma omp single
      {
        depth_index.push_back(queue.begin());
        queue.slide_window();
      }
    }
    
    */
        int *tile1_ptr = get_cacheable_tile_pointer<int>(tile1);
        int *tile0_ptr = get_cacheable_tile_pointer<int>(tile0);
        int *tilecond_ptr = get_cacheable_tile_pointer<int>(tile_cond);
        NodeID *vdata = get_cacheable_tile_pointer<NodeID>(tilev);
        while (!queue.empty()) {
            depth++;
            NodeID *base = queue.shared + queue.shared_out_start;
            int len = queue.size();

            // if len > 4 * TILE_SIZE, use MAA
            if (len > 4 * TILE_SIZE) {
                // regMax used for max
                maa_const<int>(len, regMax);
                // regDepth used for depth
                maa_const<int>(depth, regDepth);

#pragma omp for nowait
                for (int idx = 0; idx < len; idx += TILE_SIZE) {
                    SGOffset *vaddrOffset = get_cacheable_tile_pointer<SGOffset>(tilej);
#ifndef MAA_FULL2
                    int *idata = get_cacheable_tile_pointer<int>(tilei);
#endif
                    maa_const<int>(idx, reg2);

                    maa_stream_load<NodeID>(base, reg2, regMax, reg0, tile0);
                    // tilelb,2 would hold out_index[u]
                    maa_indirect_load<SGOffset>(VertexOffsetsData, tile0, tilelb);
                    // tileub,4 would hold out_index[u]
                    maa_indirect_load<SGOffset>(&VertexOffsetsData[1], tile0, tileub);
                    int curr_tilej_size = 0;
                    maa_const<int>(0, last_i_reg);
                    maa_const<SGOffset>(-1, last_j_reg);
                    do {
                        maa_range_loop<SGOffset>(last_i_reg, last_j_reg, tilelb, tileub, reg0, tilei, tilej);
                        // load v
                        maa_indirect_load<NodeID>(g.out_neighbors_, tilej, tilev);
#ifdef MAA_FULL2
                        // load depths[v] to tile_cond
                        maa_indirect_load<int>(depths.data(), tilev, tile_cond);
                        // compare tile_cond with -1 => tile1
                        maa_alu_scalar<int>(tile_cond, reg1, tile1, Operation_t::EQ_OP);
                        // add tile_cond_ptr[j] == depth -> tile0
                        maa_alu_scalar<int>(tile_cond, regDepth, tile0, Operation_t::EQ_OP);
                        // add tile0 || tile1 -> tile_cond
                        maa_alu_vector<uint32_t>(tile0, tile1, tile_cond, Operation_t::OR_OP);
                        // indirect store depth to v, dump intermediate data to tile0
                        maa_indirect_store_scalar<int>(depths.data(), tilev, regDepth, tile1, tile0);
                        wait_ready(tile0);
                        curr_tilej_size = get_tile_size(tilej);
                        for (int j = 0; j < curr_tilej_size; j++) {
                            NodeID &v = vdata[j];
                            if (tile1_ptr[j] && tile0_ptr[j] == -1) {
                                lqueue.push_back(v);
                            }
                            if (tilecond_ptr[j]) {
                                succ.set_bit_atomic(vaddrOffset[j]);
                                // #pragma omp atomic
                                // path_counts[v] += path_counts[u];
                            }
                        }

                        //set_bit_atomic store old_val | ((uint32_t)1l << bit_offset(pos)); to tile0
                        // tile0: vidx >> 5
                        // tile5: succ.start_[tile0]
                        // tile1: vidx & 31
                        // tile8: 1 << tile1
                        // tile1: tile5 | tile8
                        // scatter start_[tile0] with tile1
                        // maa_alu_scalar<uint32_t>(tilej, regFive, tile0, Operation_t::SHR_OP, tile_cond);
                        // maa_indirect_load<uint32_t>(succ.start_, tile0, tile5, tile_cond);
                        // maa_alu_scalar<uint32_t>(tilej, reg31, tile1, Operation_t::AND_OP, tile_cond);
                        // maa_alu_vector<uint32_t>(ONE_TILE, tile1, path_cnt_u, Operation_t::SHL_OP, tile_cond);
                        // maa_alu_vector<uint32_t>(tile5, path_cnt_u, tile1, Operation_t::OR_OP, tile_cond);
                        // maa_indirect_store<uint32_t>(succ.start_, tile0, tile1, tile_cond);

                        // path_counts[v] += path_counts[u]; if tile_cond
                        // first load u to tile0
                        maa_indirect_load<NodeID>(base + idx, tilei, tile0, tile_cond);
                        // then load path_counts[u] to tile1
                        maa_indirect_load<CountT>(path_counts.data(), tile0, tile1, tile_cond);
                        // then rmw path_counts[v] with path_counts[u]
                        maa_indirect_rmw_vector<CountT>(path_counts.data(), tilev, tile1, Operation_t::ADD_OP, tile_cond);

#else
                        for (int j = 0; j < curr_tilej_size; j++) {
                            NodeID &v = vdata[j];
                            NodeID &u = udata[idata[j]];
                            // std::cout << "u: " << u << " v: " << v << std::endl;
                            SGOffset voffset = vaddrOffset[j];
                            if ((depths[v] == -1) &&
                                (compare_and_swap(depths[v], static_cast<NodeID>(-1), depth))) {
                                lqueue.push_back(v);
                            }
                            if (depths[v] == depth) {
                                succ.set_bit_atomic(voffset);
#pragma omp atomic
                                path_counts[v] += path_counts[u];
                            }
                        }
#endif
                    } while (curr_tilej_size > 0);
                } // TILING
                lqueue.flush();
#pragma omp barrier
#pragma omp single
                {
                    depth_index.push_back(queue.begin_idx());
                    queue.slide_window();
                }
            } else { // else use base
#pragma omp for schedule(dynamic, 64) nowait
                for (int i = 0; i < len; i++) {
                    NodeID u = base[i];
                    for (SGOffset vidx = VertexOffsetsData[u]; vidx < VertexOffsetsData[u + 1]; vidx++) {
                        NodeID &v = g.out_neighbors_[vidx];
                        if ((depths[v] == -1) &&
                            (compare_and_swap(depths[v], static_cast<NodeID>(-1), depth))) {
                            lqueue.push_back(v);
                        }
                        if (depths[v] == depth) {
                            succ.set_bit_atomic(vidx);
#pragma omp atomic
                            path_counts[v] += path_counts[u];
                        }
                    }
                }
                lqueue.flush();
#pragma omp barrier
#pragma omp single
                {
                    depth_index.push_back(queue.begin_idx());
                    queue.slide_window();
                }
            }
        }
    }
    depth_index.push_back(queue.begin_idx());
}

#endif