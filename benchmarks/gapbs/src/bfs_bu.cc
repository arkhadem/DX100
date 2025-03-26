int64_t BUStep(const Graph &g, pvector<SGOffset> &VertexOffsets, pvector<NodeID> &parent, Bitmap &front, Bitmap &next, int num_nodes, int num_edges) {
    int64_t awake_count = 0;
    next.reset();
#ifdef GEM5
    clear_mem_region();
    add_mem_region(VertexOffsets.start_, &VertexOffsets.start_[num_nodes + 1]); // 6
    add_mem_region(g.out_neighbors_, &g.out_neighbors_[num_edges]);             // 7
    add_mem_region(parent.start_, &parent.start_[num_nodes]);                   // 8
#endif
    // #pragma omp parallel for reduction(+ : awake_count) schedule(dynamic, 1024)
    for (NodeID u = 0; u < g.num_nodes(); u++) {
        if (parent[u] < 0) {
            for (int j = VertexOffsets[u]; j < VertexOffsets[u + 1]; j++) {
                NodeID v = g.in_neighbors_[j];
                // if (front.get_bit(v) && !next.get_bit(u)) {
                //     parent[u] = v;
                //     awake_count++;
                //     next.set_bit(u);
                // }
                if (front.get_bit(v)) {
                    parent[u] = v;
                    awake_count++;
                    next.set_bit(u);
                    break;
                }
            }
        }
    }
    // // IDX_TILE store 0 to 16K
    // int tileJ, tileJp1, tileUB, tile1, tile2, tile3, tile4, tile5, tile6, tileParent, tileCond2, tileCond1;
    // int reg0, reg1, regOne, reg3, reg4, reg5, last_i_reg, last_j_reg;
    // maa_const(g.num_nodes(), reg1);
    // maa_const(1, regOne);
    // maa_const(5, reg3);
    // maa_const(31, reg4);
    // maa_const(0, reg5);
    // for (NodeID u = 0; u < g.num_nodes(); u+= TILE_SIZE) {
    //     maa_const(u, reg0);
    //     // load parent[u]
    //     maa_stream_load<NodeID>(parent.data(), reg0, reg1, regOne, tileParent);
    //     // calculate (parent[u] < 0)
    //     maa_alu_scalar<NodeID>(tileCond2, reg5, tileCond2, Operation_t::LT_OP);
    //     // j = VertexOffsets[u:u+16K]
    //     maa_stream_load<NodeID>(VertexOffsetsData, reg0, reg1, regOne, tileJ, tileCond2);
    //     // load upper bounds of j
    //     maa_stream_load<NodeID>(&VertexOffsetsData[1], reg0, reg1, regOne, tileUB, tileCond2);
    //     // j < VertexOffsets[u + 1]
    //     maa_alu_vector<NodeID>(tileJ, tileUB, tileCond1, Operation_t::LT_OP, tileCond2);
    //     while (reduce_tile(tileCond1)) {
    //         // now our condition is tileCond1

    //         // NodeID v = g.in_neighbors_[j];
    //         maa_indirect_load<NodeID>(g.in_neighbors_, tileJ, tile1, tileCond1);
    //         // if (front.get_bit(v))
    //         maa_alu_scalar<int>(tile1, reg3, tile2, Operation_t::SHR_OP, tileCond1);
    //         maa_indirect_load<uint32_t>((uint32_t*)front.start_, tile2, tile3, tileCond1);
    //         maa_alu_scalar<uint32_t>(tile1, reg4, tile4, Operation_t::AND_OP, tileCond1);
    //         maa_alu_vector<uint32_t>(tile3, tile4, tile5, Operation_t::SHR_OP, tileCond1);
    //         maa_alu_scalar<uint32_t>(tile5, regOne, tileCond2, Operation_t::AND_OP, tileCond1);

    //         // conditioned by tileCond2
    //         // parent[u] = v;
    //         maa_indirect_store<NodeID>(&parent[u], IDX_TILE, tile1, tileCond2);
    //         // awake_count++;
    //         // next.set_bit(u);

    //         maa_alu_scalar<uint32_t>(tileCond1, reg5, tileCond3, Operation_t::AND_OP, tileCond2);

    //         // j++
    //         maa_alu_scalar<NodeID>(tileJ, regOne, tileJp1, Operation_t::ADD_OP, tileCond3);
    //         // j < VertexOffsets[u + 1]
    //         maa_alu_vector<NodeID>(tileJp1, tileUB, tileCond2, Operation_t::LT_OP, tileCond3);
    //         maa_alu_vector<NodeID>(tileJp1, tileUB, tileCond2, Operation_t::LT_OP, tileCond3);

    //     }

    //    bool get_bit(size_t pos) const {
//         return (start_[word_offset(pos)] >> bit_offset(pos)) & 1l;
//     }

//     static const uint64_t kBitsPerWord = 64;
//     static uint64_t word_offset(size_t n) { return n / kBitsPerWord; }
//     static uint64_t bit_offset(size_t n) { return n & (kBitsPerWord - 1); }
// if (parent[u] < 0) {
//     for (int j = VertexOffsets[u]; j < VertexOffsets[u + 1]; j++) {
//         NodeID v = g.in_neighbors_[j];
//         // if (front.get_bit(v) && !next.get_bit(u)) {
//         //     parent[u] = v;
//         //     awake_count++;
//         //     next.set_bit(u);
//         // }
//          if (front.get_bit(v)) {
//             parent[u] = v;
//             awake_count++;
//             next.set_bit(u);
//             break;
//         }
//     }
// }
#ifdef GEM5
    clear_mem_region();
#endif
    return awake_count;
}