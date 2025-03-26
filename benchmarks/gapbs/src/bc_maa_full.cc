#ifdef MAA_FULL
pvector<ScoreT> BrandesMaa(const Graph &g, SourcePicker<Graph> &sp, NodeID num_iters, bool logging_enabled = false) {
    Timer t;
    t.Start();
    alloc_MAA();
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
        t.Stop();
        if (logging_enabled)
            PrintStep("b", t.Seconds());
        pvector<ScoreT> deltas(g.num_nodes(), 0);
        t.Start();
        init_MAA();
        // TODO: change it to 4 after pulling
        omp_set_num_threads(4);
        cout << "num_threads: " << omp_get_max_threads() << endl;
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

        for (int d = depth_index.size() - 2; d >= 0; d--) {
#pragma omp parallel
            {
                int tilev, tileu, tile_ub_d, tile_lb_d, tilei, tilej;
                int reg0, reg1, regOne, reg3, reg4, reg5, last_i_reg, last_j_reg;
                int thread_id = omp_get_thread_num();
                tilev = tiles0[thread_id];
                tileu = tiles1[thread_id];
                tile_lb_d = tiles2[thread_id];
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
                uint32_t *u_ptr = get_cacheable_tile_pointer<uint32_t>(tileu);
                uint32_t *v_ptr = get_cacheable_tile_pointer<uint32_t>(tilev);
                uint32_t *j_ptr = get_cacheable_tile_pointer<uint32_t>(tilej);
#pragma omp for schedule(dynamic, 64)
                for (int it = depth_index[d]; it < depth_index[d + 1]; it += TILE_SIZE) {
                    maa_const<int>(it, reg0);
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
                        // load path u using itile: first indirect load u
                        maa_indirect_load<int>((NodeID *)(queue.shared + it), tilei, tileu);
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
                        maa_indirect_load<uint32_t>(succ.start_, tile3, tilev);
                        maa_alu_scalar<uint32_t>(tilej, reg4, tile_cond, Operation_t::AND_OP);
                        maa_alu_vector<uint32_t>(tilev, tile_cond, tile3, Operation_t::SHR_OP);
                        maa_alu_scalar<uint32_t>(tile3, regOne, tile_cond, Operation_t::AND_OP);

                        // // if (succ.get_bit(vidx)) {
                        // // first conditionally load deltas[v]

                        maa_indirect_load<float>(deltas.data(), tile0, tile3, tile_cond);
                        maa_alu_scalar<float>(tile3, reg5, tile_dalta_v, Operation_t::ADD_OP, tile_cond);
                        // then load path v
                        maa_indirect_load<CountT>(path_counts_data, tile0, tile3, tile_cond);
                        maa_indirect_load<CountT>(path_counts_data, tileu, tilei, tile_cond);
                        // perform division of path_counts[u] / path_counts[v]
                        maa_alu_vector<CountT>(tilei, tile3, tile_path_counts, Operation_t::DIV_OP, tile_cond);
                        // multiply by (1 + deltas[v])
                        maa_alu_vector<float>(tile_path_counts, tile_dalta_v, tile3, Operation_t::MUL_OP, tile_cond);
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
#endif