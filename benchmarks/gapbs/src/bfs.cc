// Copyright (c) 2015, The Regents of the University of California (Regents)
// See LICENSE.txt for license details

#include <iostream>
#include <omp.h>
#include <vector>

#include "benchmark.h"
#include "bitmap.h"
#include "builder.h"
#include "command_line.h"
#include "graph.h"
#include "platform_atomics.h"
#include "pvector.h"
#include "sliding_queue.h"
#include "timer.h"

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
#include <MAA_utility.hpp>

/*
GAP Benchmark Suite
Kernel: Breadth-First Search (BFS)
Author: Scott Beamer

Will return parent array for a BFS traversal from a source vertex

This BFS implementation makes use of the Direction-Optimizing approach [1].
It uses the alpha and beta parameters to determine whether to switch search
directions. For representing the frontier, it uses a SlidingQueue for the
top-down approach and a Bitmap for the bottom-up approach. To reduce
false-sharing for the top-down approach, thread-local QueueBuffer's are used.

To save time computing the number of edges exiting the frontier, this
implementation precomputes the degrees in bulk at the beginning by storing
them in the parent array as negative numbers. Thus, the encoding of parent is:
  parent[x] < 0 implies x is unvisited and parent[x] = -out_degree(x)
  parent[x] >= 0 implies x been visited

[1] Scott Beamer, Krste Asanović, and David Patterson. "Direction-Optimizing
    Breadth-First Search." International Conference on High Performance
    Computing, Networking, Storage and Analysis (SC), Salt Lake City, Utah,
    November 2012.
*/

using namespace std;

int tiles0[NUM_CORES], tiles1[NUM_CORES], tiles2[NUM_CORES], tiles3[NUM_CORES], tiles4[NUM_CORES], tiles5[NUM_CORES], tilesi[NUM_CORES], tilesj[NUM_CORES];
int regs0[NUM_CORES], regs1[NUM_CORES], regs2[NUM_CORES], regs3[NUM_CORES], regs4[NUM_CORES], regs5[NUM_CORES], last_i_regs[NUM_CORES], last_j_regs[NUM_CORES];

void TDStepMAA(const Graph &g, pvector<SGOffset> &VertexOffsets, pvector<NodeID> &parent, SlidingQueue<NodeID> &queue, int num_nodes, int num_edges) {
#ifdef GEM5
    clear_mem_region();
    add_mem_region(queue.beginp(), queue.endp());                   // 6
    add_mem_region(VertexOffsets.beginp(), VertexOffsets.endp());   // 7
    add_mem_region(g.out_neighbors_, &g.out_neighbors_[num_edges]); // 8
    add_mem_region(parent.beginp(), parent.endp());                 // 9
#endif
#pragma omp parallel
    {
        int tile0, tile1, tile2, tile3, tile4, tile5, tile6, tile7;
        int reg0, reg1, regOne, regZero, last_i_reg, last_j_reg;
        int tid = omp_get_thread_num();
        tile0 = tiles0[tid];
        tile1 = tiles1[tid];
        tile2 = tiles2[tid];
        tile3 = tiles3[tid];
        tile4 = tiles4[tid];
        tile5 = tiles5[tid];
        tile6 = tilesi[tid];
        tile7 = tilesj[tid];
        reg0 = regs0[tid];
        reg1 = regs1[tid];
        regOne = regs2[tid];
        regZero = regs3[tid];
        last_i_reg = last_i_regs[tid];
        last_j_reg = last_j_regs[tid];
        QueueBuffer<NodeID> lqueue(queue);
        maa_const<int>(1, regOne);
        maa_const<int>(0, regZero);
        __restrict__ uint32_t *tile0Ptr = get_cacheable_tile_pointer<uint32_t>(tile0);
        __restrict__ uint32_t *tile4Ptr = get_cacheable_tile_pointer<uint32_t>(tile4);
        int *tile5Ptr = get_cacheable_tile_pointer<int>(tile5);
        int idx = (int)queue.shared_out_start;
        int len = (int)queue.shared_out_end;
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
                //                 std::cout << "Starting TDSTEP: tile: " << tile_size << " idx: " << idx << " (maa)" << std::endl;
                int min = idx + tid * tile_size;
                int max = min + tile_size;
                maa_const<int>((int)min, reg0);
                maa_const<int>((int)max, reg1);
                // tile0 = u = queue.shared[i]
                maa_stream_load(queue.shared, reg0, reg1, regOne, tile0);
                // tile1 = VertexOffsets[u]
                maa_indirect_load<int>(VertexOffsets.start_, tile0, tile1);
                // tile2 = VertexOffsets[u+1]
                maa_indirect_load<int>(&VertexOffsets.start_[1], tile0, tile2);
                // do while using range loop api
                maa_const<int>(0, last_i_reg);
                maa_const<int>(-1, last_j_reg);
                int curr_tile7_size = 0;
                do {
                    // tile6 = i
                    // tile7 = j
                    maa_range_loop<int>(last_i_reg, last_j_reg, tile1, tile2, regOne, tile6, tile7);
                    // tile0 = v = g.out_neighbors_[j]
                    maa_indirect_load<int>(g.out_neighbors_, tile7, tile0);
                    // tile3 = inner u = queue.shared[i]
                    maa_indirect_load<int>((NodeID *)(queue.shared + min), tile6, tile3);
                    // Transfer tile7, tile0, tile3
                    wait_ready(tile7);
                    curr_tile7_size = get_tile_size(tile7);
                    if (curr_tile7_size == 0) {
                        break;
                    }
#pragma omp critical
                    {
                        // tile7 = parent[v]
                        maa_indirect_load<int>(parent.data(), tile0, tile7);
                        // tile4 = parent[v] < 0
                        maa_alu_scalar<int>(tile7, regZero, tile4, Operation_t::LT_OP);
                        // if (tile4 -- parent[v] < 0)
                        //     tile5 = parent[v]
                        //     parent[v] = inner u
                        maa_indirect_store_vector<int>(parent.data(), tile0, tile3, tile4, tile5);
                        wait_ready(tile3);
                        // #pragma omp simd aligned(tile4Ptr, tile0Ptr, tile5Ptr : 16) simdlen(4)
                        //                     for (int i = 0; i < curr_tile7_size; i++) {
                        //                         if (tile4Ptr[i] && tile5Ptr[i] < 0) {
                        //                             std::cout << "v:" << tile0Ptr[i] << " op[v]<0: " << tile4Ptr[i] << " np[v] " << tile5Ptr[i] << std::endl;
                        //                         }
                        //                     }
                    }
#pragma omp simd aligned(tile4Ptr, tile0Ptr, tile5Ptr : 16) simdlen(4)
                    for (int i = 0; i < curr_tile7_size; i++) {
                        if (tile4Ptr[i] && tile5Ptr[i] < 0) {
                            lqueue.push_back(tile0Ptr[i]);
                        }
                    }
                } while (curr_tile7_size > 0);
                idx += NUM_CORES * tile_size;
            } else {
#pragma omp barrier
// #pragma omp master
//                 std::cout << "Starting TDSTEP: tile: " << tile_size << " idx: " << idx << " (base)" << std::endl;
#pragma omp for schedule(dynamic, 64)
                for (int i = idx; i < len; i++) {
                    NodeID u = queue.shared[i];
                    for (int j = VertexOffsets[u]; j < VertexOffsets[u + 1]; j++) {
                        NodeID v = g.out_neighbors_[j];
                        NodeID curr_val = parent[v];
                        if (curr_val < 0) {
                            // indirect write (parent, v_tile, u_tile)
                            // core fetch v tile
                            if (compare_and_swap(parent[v], curr_val, u)) {
                                parent[v] = u;
                                lqueue.push_back(v);
                            }
                        }
                    }
                }
                idx = len;
            }
        }

        lqueue.flush();
    }
#ifdef GEM5
    clear_mem_region();
#endif
}

void TDStep(const Graph &g, pvector<SGOffset> &VertexOffsets, pvector<NodeID> &parent, SlidingQueue<NodeID> &queue, int num_nodes, int num_edges) {
#ifdef GEM5
    clear_mem_region();
    add_mem_region(queue.beginp(), queue.endp());                   // 6
    add_mem_region(VertexOffsets.beginp(), VertexOffsets.endp());   // 7
    add_mem_region(g.out_neighbors_, &g.out_neighbors_[num_edges]); // 8
    add_mem_region(parent.beginp(), parent.endp());                 // 9
#endif
#pragma omp parallel
    {
        QueueBuffer<NodeID> lqueue(queue);
#pragma omp for nowait
        for (size_t i = queue.shared_out_start; i < queue.shared_out_end; i++) {
            NodeID u = queue.shared[i];
            for (int j = VertexOffsets[u]; j < VertexOffsets[u + 1]; j++) {
                NodeID v = g.out_neighbors_[j];
                NodeID curr_val = parent[v];
                if (curr_val < 0) {
                    // indirect write (parent, v_tile, u_tile)
                    // core fetch v tile
                    if (compare_and_swap(parent[v], curr_val, u)) {
                        parent[v] = u;
                        lqueue.push_back(v);
                    }
                }
            }
        }
        lqueue.flush();
    }
#ifdef GEM5
    clear_mem_region();
#endif
}

int64_t TDStep2(const Graph &g, pvector<NodeID> &parent, SlidingQueue<NodeID> &queue) {
    int64_t scout_count = 0;
#pragma omp parallel
    {
        QueueBuffer<NodeID> lqueue(queue);
#pragma omp for reduction(+ : scout_count) nowait
        for (auto q_iter = queue.begin(); q_iter < queue.end(); q_iter++) {
            NodeID u = *q_iter;
            for (NodeID v : g.out_neigh(u)) {
                NodeID curr_val = parent[v];
                if (curr_val < 0) {
                    if (compare_and_swap(parent[v], curr_val, u)) {
                        lqueue.push_back(v);
                        scout_count += -curr_val;
                    }
                }
            }
        }
        lqueue.flush();
    }
    return scout_count;
}

void QueueToBitmap(const SlidingQueue<NodeID> &queue, Bitmap &bm) {
#pragma omp parallel for
    for (auto q_iter = queue.begin(); q_iter < queue.end(); q_iter++) {
        NodeID u = *q_iter;
        bm.set_bit_atomic(u);
    }
}

void BitmapToQueue(const Graph &g, const Bitmap &bm,
                   SlidingQueue<NodeID> &queue) {
#pragma omp parallel
    {
        QueueBuffer<NodeID> lqueue(queue);
#pragma omp for nowait
        for (NodeID n = 0; n < g.num_nodes(); n++)
            if (bm.get_bit(n))
                lqueue.push_back(n);
        lqueue.flush();
    }
    queue.slide_window();
}

pvector<NodeID> InitParent(const Graph &g) {
    pvector<NodeID> parent(g.num_nodes());
#pragma omp parallel for
    for (NodeID n = 0; n < g.num_nodes(); n++)
        parent[n] = g.out_degree(n) != 0 ? -g.out_degree(n) : -1;
    return parent;
}

pvector<NodeID> DOBFS(const Graph &g, NodeID source, bool logging_enabled = false,
                      int alpha = 1, int beta = 18) {
    int num_nodes = g.num_nodes();
    int num_edges = g.num_edges_directed();
    if (logging_enabled)
        PrintStep("Source", static_cast<int64_t>(source));
    pvector<SGOffset> VertexOffsetsOut = g.VertexOffsets();
    Timer t;
    alloc_MAA();
    init_MAA();
    t.Start();
    pvector<NodeID> parent = InitParent(g);
    t.Stop();
    if (logging_enabled)
        PrintStep("i", t.Seconds());
    parent[source] = source;
    SlidingQueue<NodeID> queue(g.num_nodes());
    queue.push_back(source);
    queue.slide_window();
    Bitmap curr(g.num_nodes());
    curr.reset();
    Bitmap front(g.num_nodes());
    front.reset();

#ifdef GEM5
    std::cout << "ROI started: " << omp_get_num_threads() << " threads" << std::endl;
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
    while (!queue.empty()) {
        std::cout << "Starting TDStep: " << queue.size() << " elements" << std::endl;
        t.Start();
        TDStep(g, VertexOffsetsOut, parent, queue, num_nodes, num_edges);
        queue.slide_window();
        t.Stop();
        if (logging_enabled) {
            PrintStep("td", t.Seconds());
        }
    }
#pragma omp parallel for
    for (NodeID n = 0; n < g.num_nodes(); n++)
        if (parent[n] < -1)
            parent[n] = -1;
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
    std::cout << "ROI End!!!" << std::endl;
    m5_exit(0);
#endif
    return parent;
}

pvector<NodeID> DOBFSMAA(const Graph &g, NodeID source, bool logging_enabled = false,
                         int alpha = 1, int beta = 18) {
    int num_nodes = g.num_nodes();
    int num_edges = g.num_edges_directed();
    if (logging_enabled)
        PrintStep("Source", static_cast<int64_t>(source));
    pvector<SGOffset> VertexOffsetsOut = g.VertexOffsets();
    Timer t;
    alloc_MAA();
    init_MAA();
    t.Start();
#pragma omp parallel
    {
#pragma omp critical
        {
            int tid = omp_get_thread_num();
            tiles0[tid] = get_new_tile<int>();
            tiles1[tid] = get_new_tile<int>();
            tiles2[tid] = get_new_tile<int>();
            tiles3[tid] = get_new_tile<int>();
            tiles4[tid] = get_new_tile<int>();
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
    pvector<NodeID> parent = InitParent(g);
    t.Stop();
    if (logging_enabled)
        PrintStep("i", t.Seconds());
    parent[source] = source;
    SlidingQueue<NodeID> queue(g.num_nodes());
    queue.push_back(source);
    queue.slide_window();
    Bitmap curr(g.num_nodes());
    curr.reset();
    Bitmap front(g.num_nodes());
    front.reset();

#ifdef GEM5
    std::cout << "ROI started: " << omp_get_num_threads() << " threads" << std::endl;
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
    while (!queue.empty()) {
        t.Start();
        bool isMAA = true;
        std::cout << "Starting TDStepMAA: " << queue.size() << " elements" << std::endl;
        TDStepMAA(g, VertexOffsetsOut, parent, queue, num_nodes, num_edges);
        queue.slide_window();
        t.Stop();
        if (logging_enabled) {
            if (isMAA)
                PrintStep("td_maa", t.Seconds());
            else
                PrintStep("td", t.Seconds());
        }
    }
#pragma omp parallel for
    for (NodeID n = 0; n < g.num_nodes(); n++)
        if (parent[n] < -1)
            parent[n] = -1;
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
    std::cout << "ROI End!!!" << std::endl;
    m5_exit(0);
#endif
    return parent;
}

void PrintBFSStats(const Graph &g, const pvector<NodeID> &bfs_tree) {
    int64_t tree_size = 0;
    int64_t n_edges = 0;
    for (NodeID n : g.vertices()) {
        if (bfs_tree[n] >= 0) {
            n_edges += g.out_degree(n);
            tree_size++;
        }
    }
    cout << "BFS Tree has " << tree_size << " nodes and ";
    cout << n_edges << " edges" << endl;
}

// BFS verifier does a serial BFS from same source and asserts:
// - parent[source] = source
// - parent[v] = u  =>  depth[v] = depth[u] + 1 (except for source)
// - parent[v] = u  => there is edge from u to v
// - all vertices reachable from source have a parent
bool BFSVerifier(const Graph &g, NodeID source,
                 const pvector<NodeID> &parent) {
    pvector<int> depth(g.num_nodes(), -1);
    depth[source] = 0;
    vector<NodeID> to_visit;
    to_visit.reserve(g.num_nodes());
    to_visit.push_back(source);
    for (auto it = to_visit.begin(); it != to_visit.end(); it++) {
        NodeID u = *it;
        for (NodeID v : g.out_neigh(u)) {
            if (depth[v] == -1) {
                depth[v] = depth[u] + 1;
                to_visit.push_back(v);
            }
        }
    }
    for (NodeID u : g.vertices()) {
        if ((depth[u] != -1) && (parent[u] != -1)) {
            if (u == source) {
                if (!((parent[u] == u) && (depth[u] == 0))) {
                    cout << "Source wrong" << endl;
                    return false;
                }
                continue;
            }
            bool parent_found = false;
            for (NodeID v : g.in_neigh(u)) {
                if (v == parent[u]) {
                    if (depth[v] != depth[u] - 1) {
                        cout << "Wrong depths for " << u << " & " << v << endl;
                        return false;
                    }
                    parent_found = true;
                    break;
                }
            }
            if (!parent_found) {
                cout << "Couldn't find edge from " << parent[u] << " to " << u << endl;
                return false;
            }
        } else if (depth[u] != parent[u]) {
            cout << "Reachability mismatch" << endl;
            return false;
        }
    }
    return true;
}

int main(int argc, char *argv[]) {
    CLApp cli(argc, argv, "breadth-first search");
    if (!cli.ParseArgs())
        return -1;
    Builder b(cli);
    Graph g = b.MakeGraph();
    SourcePicker<Graph> sp(g, cli.start_vertex());
#ifdef GEM5
    std::cout << "Fake Checkpoint started" << std::endl;
    m5_checkpoint(0, 0);
    std::cout << "Fake Checkpoint ended" << std::endl;
#endif
    auto BFSBound = [&sp, &cli](const Graph &g) {
#ifdef MAA
        return DOBFSMAA(g, sp.PickNext(), cli.logging_en());
#else
        return DOBFS(g, sp.PickNext(), cli.logging_en());
#endif
    };
    SourcePicker<Graph> vsp(g, cli.start_vertex());
    auto VerifierBound = [&vsp](const Graph &g, const pvector<NodeID> &parent) {
        return BFSVerifier(g, vsp.PickNext(), parent);
    };
    BenchmarkKernel(cli, g, BFSBound, PrintBFSStats, VerifierBound);
    return 0;
}
