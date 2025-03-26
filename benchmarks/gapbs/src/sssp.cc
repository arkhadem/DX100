// Copyright (c) 2015, The Regents of the University of California (Regents)
// See LICENSE.txt for license details

#include <cinttypes>
#include <cstdio>
#include <limits>
#include <iostream>
#include <omp.h>
#include <queue>
#include <vector>

#include "MAA.hpp"
#include "benchmark.h"
#include "builder.h"
#include "command_line.h"
#include "graph.h"
#include "platform_atomics.h"
#include "pvector.h"
#include "timer.h"

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
Kernel: Single-source Shortest Paths (SSSP)
Author: Scott Beamer, Yunming Zhang

Returns array of distances for all vertices from given source vertex

This SSSP implementation makes use of the ∆-stepping algorithm [1]. The type
used for weights and distances (WeightT) is typedefined in benchmark.h. The
delta parameter (-d) should be set for each input graph. This implementation
incorporates a new bucket fusion optimization [2] that significantly reduces
the number of iterations (& barriers) needed.

The bins of width delta are actually all thread-local and of type std::vector,
so they can grow but are otherwise capacity-proportional. Each iteration is
done in two phases separated by barriers. In the first phase, the current
shared bin is processed by all threads. As they find vertices whose distance
they are able to improve, they add them to their thread-local bins. During this
phase, each thread also votes on what the next bin should be (smallest
non-empty bin). In the next phase, each thread copies its selected
thread-local bin into the shared bin.

Once a vertex is added to a bin, it is not removed, even if its distance is
later updated and, it now appears in a lower bin. We find ignoring vertices if
their distance is less than the min distance for the current bin removes
enough redundant work to be faster than removing the vertex from older bins.

The bucket fusion optimization [2] executes the next thread-local bin in
the same iteration if the vertices in the next thread-local bin have the
same priority as those in the current shared bin. This optimization greatly
reduces the number of iterations needed without violating the priority-based
execution order, leading to significant speedup on large diameter road networks.

[1] Ulrich Meyer and Peter Sanders. "δ-stepping: a parallelizable shortest path
    algorithm." Journal of Algorithms, 49(1):114–152, 2003.

[2] Yunming Zhang, Ajay Brahmakshatriya, Xinyi Chen, Laxman Dhulipala,
    Shoaib Kamil, Saman Amarasinghe, and Julian Shun. "Optimizing ordered graph
    algorithms with GraphIt." The 18th International Symposium on Code Generation
    and Optimization (CGO), pages 158-170, 2020.
*/

using namespace std;

const WeightT kDistInf = numeric_limits<WeightT>::max() / 2;
const size_t kMaxBin = numeric_limits<size_t>::max() / 2;
const size_t kBinSizeThreshold = 1000;

pvector<WeightT> DeltaStepMAA(const WGraph &g, NodeID source, WeightT delta, bool logging_enabled = false) {
    int num_directed_edges = g.num_edges_directed();
    int num_nodes = g.num_nodes();

    std::cout << "SSSP: num_nodes: " << num_nodes << ", num_edges: " << num_directed_edges << ", edge/node: " << (double)num_nodes / (double)num_directed_edges << ", source: " << source << std::endl;
    pvector<WeightT> dist(num_nodes, kDistInf);
    pvector<NodeID> frontier(num_directed_edges);
    pvector<SGOffset> VertexOffsets = g.VertexOffsets();
    dist[source] = 0;
    // two element arrays for double buffering curr=iter&1, next=(iter+1)&1
    size_t shared_indexes[2] = {0, kMaxBin};
    size_t frontier_tails[2] = {1, 0};
    frontier[0] = source;
    alloc_MAA();
    init_MAA();

#ifdef GEM5
    clear_mem_region();
    add_mem_region(frontier.beginp(), frontier.endp());                            // 6
    add_mem_region(dist.beginp(), dist.endp());                                    // 7
    add_mem_region(VertexOffsets.beginp(), VertexOffsets.endp());                  // 8
    add_mem_region(g.out_neighbors_, &g.out_neighbors_[VertexOffsets[num_nodes]]); // 9
    std::cout << "ROI started: " << omp_get_num_threads() << " threads" << std::endl;
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

#ifdef TIMER_ENABLED
    Timer t;
    t.Start();
#endif
#pragma omp parallel
    {
        int tilev, tileu, tile_ub_d, tile_lb_d, tilei, tile1, tileCond, tile2;
        int reg0, reg1, regOne, regTwo, reg2, last_i_reg, last_j_reg;

#pragma omp critical
        {
            tilev = get_new_tile<int>();
            tileu = get_new_tile<int>();
            tile_ub_d = get_new_tile<int>();
            tile_lb_d = get_new_tile<int>();
            tilei = get_new_tile<int>();
            tile1 = get_new_tile<int>();
            tile2 = get_new_tile<int>();
            tileCond = get_new_tile<int>();
            reg0 = get_new_reg<int>();
            reg1 = get_new_reg<int>();
            reg2 = get_new_reg<int>();
            regOne = get_new_reg<int>(1);
            regTwo = get_new_reg<int>(2);
            last_i_reg = get_new_reg<int>();
            last_j_reg = get_new_reg<int>();
        }
        vector<vector<NodeID>> local_bins(0);
        size_t iter = 0;
        int *tilev_ptr = get_cacheable_tile_pointer<int>(tilev);
        int *tile1_ptr = get_cacheable_tile_pointer<int>(tile1);
        int *tilei_ptr = get_cacheable_tile_pointer<int>(tilei);
        while (shared_indexes[iter & 1] != kMaxBin) {
            size_t &curr_bin_index = shared_indexes[iter & 1];
            size_t &next_bin_index = shared_indexes[(iter + 1) & 1];
            size_t &curr_frontier_tail = frontier_tails[iter & 1];
            size_t &next_frontier_tail = frontier_tails[(iter + 1) & 1];
            if ((int)curr_frontier_tail < NUM_CORES * 1024) {
#pragma omp master
                std::cout << "Starting DeltaStepMAA: " << curr_frontier_tail << " elements (base)" << std::endl;
#pragma omp for
                for (size_t i = 0; i < curr_frontier_tail; i++) {
                    NodeID u = frontier[i];
                    WeightT dist_u = dist[u];
                    if (dist_u >= delta * static_cast<WeightT>(curr_bin_index)) {
                        for (int j = VertexOffsets[u]; j < VertexOffsets[u + 1]; j++) {
                            WNode wn = g.out_neighbors_[j];
                            NodeID v = wn.v;
                            WeightT old_dist = dist[v];
                            WeightT new_dist = dist_u + wn.w;
                            while (new_dist < old_dist) {
                                if (compare_and_swap(dist[v], old_dist, new_dist)) {
                                    size_t dest_bin = new_dist / delta;
                                    if (dest_bin >= local_bins.size())
                                        local_bins.resize(dest_bin + 1);
                                    local_bins[dest_bin].push_back(v);
                                    // printf("Node[%d] = %d\n", v, new_dist);
                                    break;
                                }
                                old_dist = dist[v]; // swap failed, recheck dist update & retry
                            }
                        }
                    }
                } //
            } else {
                const int cft = (int)curr_frontier_tail;
#if TILE_SIZE == 32768
                const int tile_size = cft > NUM_CORES * 32768   ? 32768
                                      : cft > NUM_CORES * 16384 ? 16384
                                      : cft > NUM_CORES * 8192  ? 8192
                                      : cft > NUM_CORES * 4096  ? 4096
                                      : cft > NUM_CORES * 2048  ? 2048
                                                                : 1024;
#elif TILE_SIZE == 16384
                const int tile_size = cft > NUM_CORES * 16384  ? 16384
                                      : cft > NUM_CORES * 8192 ? 8192
                                      : cft > NUM_CORES * 4096 ? 4096
                                      : cft > NUM_CORES * 2048 ? 2048
                                                               : 1024;
#elif TILE_SIZE == 8192
                const int tile_size = cft > NUM_CORES * 8192   ? 8192
                                      : cft > NUM_CORES * 4096 ? 4096
                                      : cft > NUM_CORES * 2048 ? 2048
                                                               : 1024;
#elif TILE_SIZE == 4096
                const int tile_size = cft > NUM_CORES * 4096   ? 4096
                                      : cft > NUM_CORES * 2048 ? 2048
                                                               : 1024;
#elif TILE_SIZE == 2048
                const int tile_size = cft > NUM_CORES * 2048   ? 2048
                                      : cft > NUM_CORES * 1024 ? 1024
                                                               : 512;
#elif TILE_SIZE == 1024
                const int tile_size = cft > NUM_CORES * 1024 ? 1024
                                                             : 512;
#else
                assert(false);
#endif
                maa_const<int>(delta * static_cast<WeightT>(curr_bin_index), reg2);
#pragma omp master
                std::cout << "Starting DeltaStepMAA: " << cft << " elements (maa-" << tile_size << ")" << std::endl;
#pragma omp for
                for (int idx = 0; idx < cft; idx += tile_size) {
                    maa_const<int>(idx, reg0);
                    maa_const<int>((int)min(cft, idx + tile_size), reg1);
                    // streaming load u
                    maa_stream_load<int>(frontier.data(), reg0, reg1, regOne, tileu);
                    // load dist[u]
                    maa_indirect_load<WeightT>(dist.data(), tileu, tile1);
                    // alu ge on dist[u] and reg2
                    maa_alu_scalar<WeightT>(tile1, reg2, tileCond, Operation_t::GTE_OP);
                    // then load lower and upper bounds of VertexOffsets[u:u+TILE_SIZE] based on tileCond
                    maa_indirect_load<SGOffset>(VertexOffsets.data(), tileu, tile_lb_d, tileCond);
                    maa_indirect_load<SGOffset>(&VertexOffsets.data()[1], tileu, tile_ub_d, tileCond);
                    // do while loop
                    int curr_size = 0;
                    maa_const<int>(0, last_i_reg);
                    maa_const<int>(-1, last_j_reg);
                    do {
                        maa_range_loop<SGOffset>(last_i_reg, last_j_reg, tile_lb_d, tile_ub_d, regOne, tilei, tile1, tileCond);
                        // tile2 would be double of tile1
                        maa_alu_scalar<int>(tile1, regTwo, tile2, Operation_t::MUL_OP);
                        // load g.out_neighbors_ to node v
                        maa_indirect_load<NodeID>((NodeID *)g.out_neighbors_, tile2, tilev);
                        // load w
                        maa_indirect_load<WeightT>(((WeightT *)g.out_neighbors_ + 1), tile2, tileu);
                        // load u to tile0
                        maa_indirect_load<int>(frontier.data() + idx, tilei, tile2);
#pragma omp critical
                        {
                            // load dist[u] to tile1 using tilei
                            maa_indirect_load<WeightT>(dist.data(), tile2, tile1);
                            // do plus on dist[u] and w
                            maa_alu_vector<WeightT>(tile1, tileu, tile2, Operation_t::ADD_OP);
                            // do rmw on dist[v] with tile2 using tilev as idx, tilei is value prior to update
                            maa_indirect_rmw_vector<WeightT>(dist.data(), tilev, tile2, Operation_t::MIN_OP, -1, tilei);
                            // load distance v again
                            maa_indirect_load<WeightT>(dist.data(), tilev, tile1);
                            wait_ready(tile1);
                        }
                        // if new value = new dist
                        maa_alu_vector<WeightT>(tile2, tile1, tileu, Operation_t::EQ_OP);
                        // if new value != prev value
                        maa_alu_vector<WeightT>(tilei, tile1, tile2, Operation_t::GT_OP);
                        // if new value != prev value and new value = new dist
                        maa_alu_vector<WeightT>(tile2, tileu, tilei, Operation_t::AND_OP);
                        wait_ready(tilei);
                        curr_size = get_tile_size(tilei);
                        for (int j = 0; j < curr_size; j++) {
                            if (tilei_ptr[j]) {
                                size_t dest_bin = tile1_ptr[j] / delta;
                                if (dest_bin >= local_bins.size())
                                    local_bins.resize(dest_bin + 1);
                                local_bins[dest_bin].push_back(tilev_ptr[j]);
                                // printf("Node[%d] = %d\n", tilev_ptr[j], tile1_ptr[j]);
                            }
                        }
                    } while (curr_size > 0);
                }
            }
            if (curr_bin_index < local_bins.size() &&
                !local_bins[curr_bin_index].empty() &&
                local_bins[curr_bin_index].size() < kBinSizeThreshold) {
                assert(false);
#ifdef GEM5
                m5_exit(0);
#endif
            }
            for (size_t i = curr_bin_index; i < local_bins.size(); i++) {
                if (!local_bins[i].empty()) {
#pragma omp critical
                    next_bin_index = min(next_bin_index, i);
                    break;
                }
            }
#pragma omp barrier
#pragma omp single nowait
            {
#ifdef TIMER_ENABLED
                t.Stop();
                if (logging_enabled)
                    PrintStep(curr_bin_index, t.Millisecs(), curr_frontier_tail);
                t.Start();
#endif
                curr_bin_index = kMaxBin;
                curr_frontier_tail = 0;
            }
            if (next_bin_index < local_bins.size()) {
                size_t copy_start = fetch_and_add(next_frontier_tail, local_bins[next_bin_index].size());
                copy(local_bins[next_bin_index].begin(), local_bins[next_bin_index].end(), frontier.data() + copy_start);
                local_bins[next_bin_index].resize(0);
            }
            iter++;
#pragma omp barrier
        }
#pragma omp single
        if (logging_enabled)
            cout << "took " << iter << " iterations" << endl;
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
    clear_mem_region();
    std::cout << "ROI End!!!" << std::endl;
    m5_exit(0);
#endif
    return dist;
}

pvector<WeightT> DeltaStep(const WGraph &g, NodeID source, WeightT delta, bool logging_enabled = false) {
    int num_directed_edges = g.num_edges_directed();
    int num_nodes = g.num_nodes();

    std::cout << "SSSP: num_nodes: " << num_nodes << ", num_edges: " << num_directed_edges << ", edge/node: " << (double)num_nodes / (double)num_directed_edges << ", source: " << source << std::endl;
    pvector<WeightT> dist(num_nodes, kDistInf);
    pvector<NodeID> frontier(num_directed_edges);
    pvector<SGOffset> VertexOffsets = g.VertexOffsets();
    dist[source] = 0;
    // two element arrays for double buffering curr=iter&1, next=(iter+1)&1
    size_t shared_indexes[2] = {0, kMaxBin};
    size_t frontier_tails[2] = {1, 0};
    frontier[0] = source;
    alloc_MAA();
    init_MAA();

#ifdef GEM5
    clear_mem_region();
    add_mem_region(frontier.beginp(), frontier.endp());                            // 6
    add_mem_region(dist.beginp(), dist.endp());                                    // 7
    add_mem_region(VertexOffsets.beginp(), VertexOffsets.endp());                  // 8
    add_mem_region(g.out_neighbors_, &g.out_neighbors_[VertexOffsets[num_nodes]]); // 9
    std::cout << "ROI started: " << omp_get_num_threads() << " threads" << std::endl;
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

#ifdef TIMER_ENABLED
    Timer t;
    t.Start();
#endif
#pragma omp parallel
    {
        vector<vector<NodeID>> local_bins(0);
        size_t iter = 0;
        while (shared_indexes[iter & 1] != kMaxBin) {
            size_t &curr_bin_index = shared_indexes[iter & 1];
            size_t &next_bin_index = shared_indexes[(iter + 1) & 1];
            size_t &curr_frontier_tail = frontier_tails[iter & 1];
            size_t &next_frontier_tail = frontier_tails[(iter + 1) & 1];
#pragma omp master
            std::cout << "Starting DeltaStep: " << curr_frontier_tail << " elements (base)" << std::endl;
#pragma omp for nowait schedule(dynamic, 64)
            for (size_t i = 0; i < curr_frontier_tail; i++) {
                NodeID u = frontier[i];
                WeightT dist_u = dist[u];
                if (dist_u >= delta * static_cast<WeightT>(curr_bin_index)) {
                    for (int j = VertexOffsets[u]; j < VertexOffsets[u + 1]; j++) {
                        WNode wn = g.out_neighbors_[j];
                        NodeID v = wn.v;
                        WeightT old_dist = dist[v];
                        WeightT new_dist = dist_u + wn.w;
                        while (new_dist < old_dist) {
                            if (compare_and_swap(dist[v], old_dist, new_dist)) {
                                size_t dest_bin = new_dist / delta;
                                if (dest_bin >= local_bins.size())
                                    local_bins.resize(dest_bin + 1);
                                local_bins[dest_bin].push_back(v);
                                break;
                            }
                            old_dist = dist[v]; // swap failed, recheck dist update & retry
                        }
                    }
                }
            }
            if (curr_bin_index < local_bins.size() &&
                !local_bins[curr_bin_index].empty() &&
                local_bins[curr_bin_index].size() < kBinSizeThreshold) {
                assert(false);
            }
            for (size_t i = curr_bin_index; i < local_bins.size(); i++) {
                if (!local_bins[i].empty()) {
#pragma omp critical
                    next_bin_index = min(next_bin_index, i);
                    break;
                }
            }
#pragma omp barrier
#pragma omp single nowait
            {
#ifdef TIMER_ENABLED
                t.Stop();
                if (logging_enabled)
                    PrintStep(curr_bin_index, t.Millisecs(), curr_frontier_tail);
                t.Start();
#endif
                curr_bin_index = kMaxBin;
                curr_frontier_tail = 0;
            }
            if (next_bin_index < local_bins.size()) {
                size_t copy_start = fetch_and_add(next_frontier_tail, local_bins[next_bin_index].size());
                copy(local_bins[next_bin_index].begin(),
                     local_bins[next_bin_index].end(), frontier.data() + copy_start);
                local_bins[next_bin_index].resize(0);
            }
            iter++;
#pragma omp barrier
        }
#pragma omp single
        if (logging_enabled)
            cout << "took " << iter << " iterations" << endl;
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
    clear_mem_region();
    std::cout << "ROI End!!!" << std::endl;
    m5_exit(0);
#endif
    return dist;
}

void PrintSSSPStats(const WGraph &g, const pvector<WeightT> &dist) {
    auto NotInf = [](WeightT d) { return d != kDistInf; };
    int64_t num_reached = count_if(dist.begin(), dist.end(), NotInf);
    cout << "SSSP Tree reaches " << num_reached << " nodes" << endl;
}

// Compares against simple serial implementation
bool SSSPVerifier(const WGraph &g, NodeID source, const pvector<WeightT> &dist_to_test) {
    // Serial Dijkstra implementation to get oracle distances
    pvector<WeightT> oracle_dist(g.num_nodes(), kDistInf);
    oracle_dist[source] = 0;
    typedef pair<WeightT, NodeID> WN;
    priority_queue<WN, vector<WN>, greater<WN>> mq;
    mq.push(make_pair(0, source));
    while (!mq.empty()) {
        WeightT td = mq.top().first;
        NodeID u = mq.top().second;
        mq.pop();
        if (td == oracle_dist[u]) {
            for (WNode wn : g.out_neigh(u)) {
                if (td + wn.w < oracle_dist[wn.v]) {
                    oracle_dist[wn.v] = td + wn.w;
                    mq.push(make_pair(td + wn.w, wn.v));
                }
            }
        }
    }
    // Report any mismatches
    bool all_ok = true;
    for (NodeID n : g.vertices()) {
        if (dist_to_test[n] != oracle_dist[n]) {
            cout << n << ": " << dist_to_test[n] << " != " << oracle_dist[n] << endl;
            all_ok = false;
        }
    }
    return all_ok;
}

int main(int argc, char *argv[]) {
    CLDelta<WeightT> cli(argc, argv, "single-source shortest-path");
    if (!cli.ParseArgs())
        return -1;
    WeightedBuilder b(cli);
    WGraph g = b.MakeGraph();
    SourcePicker<WGraph> sp(g, cli.start_vertex());
#ifdef GEM5
    std::cout << "Fake Checkpoint started" << std::endl;
    m5_checkpoint(0, 0);
    std::cout << "Fake Checkpoint ended" << std::endl;
#endif
    auto SSSPBound = [&sp, &cli](const WGraph &g) {
#ifdef MAA
        return DeltaStepMAA(g, sp.PickNext(), cli.delta(), cli.logging_en());
#else
        return DeltaStep(g, sp.PickNext(), cli.delta(), cli.logging_en());
#endif
    };
    SourcePicker<WGraph> vsp(g, cli.start_vertex());
    auto VerifierBound = [&vsp](const WGraph &g, const pvector<WeightT> &dist) {
        return SSSPVerifier(g, vsp.PickNext(), dist);
    };
    BenchmarkKernel(cli, g, SSSPBound, PrintSSSPStats, VerifierBound);
    return 0;
}
