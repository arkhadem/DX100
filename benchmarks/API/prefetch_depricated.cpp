
void gather_rmw_indirectcond_indirectrangeloop_indirectcond_prefetch_maa(int *nodes, int *boundaries, float *a, float *b, int *idx, int min, int max, int stride, const float mul_const, float *cond, const float cond_const) {
    std::cout << "starting gather_rmw_indirectcond_indirectrangeloop_indirectcond_prefetch_maa min(" << min << "), max(" << max << "), stride(" << stride << ")" << std::endl;

#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif

    init_MAA();

    // 1 scalar register
    int one_reg_id = get_new_reg<int>(1);
    // max scalar register
    int max_reg_id = get_new_reg<int>(max);
    // min scalar register
    int min_reg_ids[2] = {get_new_reg<int>(), get_new_reg<int>()};
    // stride scalar register
    int stride_reg_id = get_new_reg<int>(stride);
    // cond_const scalar register
    int cond_const_reg_id = get_new_reg<float>(cond_const);
    // the last i and j values that the range loop has generated
    // used as the context for the next tile of j iterations
    int last_i_reg_id = get_new_reg<int>(0);
    int last_j_reg_id = get_new_reg<int>(-1);

    // allocate a tile of idx[j]
    int idx_tiles[2] = {get_new_tile(), get_new_tile()};
    // allocate a tile of b[idx[j]]
    int b_tiles[2] = {get_new_tile(), get_new_tile()};
    // allocate a tile of values that will be read-modified-written to a[idx[j]]
    int a_tiles[2] = {get_new_tile(), get_new_tile()};
    // allocate a tile of cond[nodes[i]] and cond[nodes[j]] values
    int condi_tiles[2] = {get_new_tile(), get_new_tile()};
    int condj_tiles[2] = {get_new_tile(), get_new_tile()};
    // allocate a tile of boundaries[nodes[i]] and boundaries[nodes[i] + 1] values
    int boundaries0_tiles[2] = {get_new_tile(), get_new_tile()};
    int boundaries1_tiles[2] = {get_new_tile(), get_new_tile()};
    // allocate a tile of i and j values that the range loop generates
    int i_tiles[2] = {get_new_tile(), get_new_tile()};
    int j_tiles[2] = {get_new_tile(), get_new_tile()};
    // allocate a tile of nodes[i] and nodes[j] values
    int nodesi_tiles[2] = {get_new_tile(), get_new_tile()};
    int nodesj_tiles[2] = {get_new_tile(), get_new_tile()};
    // allocate a tile of (cond_const < cond[nodes[i]]) values
    int condi_res_tiles[2] = {get_new_tile(), get_new_tile()};
    // allocate a tile of (cond_const < cond[nodes[j]]) values
    int condj_res_tiles[2] = {get_new_tile(), get_new_tile()};

    // get the SPD pointer to the b[idx[j]] tile
    float *b_ps[2] = {get_cacheable_tile_pointer<float>(b_tiles[0]),
                      get_cacheable_tile_pointer<float>(b_tiles[1])};
    // get the SPD address of a tile of values that will be read-modified-written to a[idx[j]]
    volatile float *a_ps[2] = {get_noncacheable_tile_pointer<float>(a_tiles[0]),
                               get_noncacheable_tile_pointer<float>(a_tiles[1])};
    // get the SPD address of the i and j tiles
    int *i_ps[2] = {get_cacheable_tile_pointer<int>(i_tiles[0]),
                    get_cacheable_tile_pointer<int>(i_tiles[1])};
    int *j_ps[2] = {get_cacheable_tile_pointer<int>(j_tiles[0]),
                    get_cacheable_tile_pointer<int>(j_tiles[1])};
    // get the SPD address of the boundaries[nodes[i]] and boundaries[nodes[i] + 1] tiles
    int *boundaries0_ps[2] = {get_cacheable_tile_pointer<int>(boundaries0_tiles[0]),
                              get_cacheable_tile_pointer<int>(boundaries0_tiles[1])};
    int *boundaries1_ps[2] = {get_cacheable_tile_pointer<int>(boundaries1_tiles[0]),
                              get_cacheable_tile_pointer<int>(boundaries1_tiles[1])};
    // get the SPD address of the cond[nodes[i]] and cond[nodes[j]] tiles
    float *condi_ps[2] = {get_cacheable_tile_pointer<float>(condi_tiles[0]),
                          get_cacheable_tile_pointer<float>(condi_tiles[1])};
    float *condj_ps[2] = {get_cacheable_tile_pointer<float>(condj_tiles[0]),
                          get_cacheable_tile_pointer<float>(condj_tiles[1])};

    int current_tile_i = 0;
    // Here we change a loop from min to max with stride to
    // a loop from 0 to i_max with stride = 1
    int i_max = (max - min - 1) / stride + 1;

    /*************** prefetch the first i tile ***************/
    int i_base = 0;
    int curr_min = i_base * stride + min;
    // i = min
    maa_const(curr_min, min_reg_ids[current_tile_i]);
    // load a tile of nodes[i] values to the nodesi SPD
    maa_stream_load<int>(nodes, min_reg_ids[current_tile_i], max_reg_id, stride_reg_id, nodesi_tiles[current_tile_i]);
    // load a tile of cond[nodes[i]] values to the condi SPD
    maa_indirect_load<float>(cond, nodesi_tiles[current_tile_i], condi_tiles[current_tile_i]);
    // if (cond_const < cond[i])
    maa_alu_scalar<float>(condi_tiles[current_tile_i], cond_const_reg_id, condi_res_tiles[current_tile_i], Operation_t::GT_OP);
    // load a tile of boundaries[nodes[i]] and boundaries[nodes[i] + 1] values
    // to the boundaries0 and boundaries1 SPD if (cond_const < cond[nodes[i]]) holds
    maa_indirect_load<int>(boundaries, nodesi_tiles[current_tile_i], boundaries0_tiles[current_tile_i], condi_res_tiles[current_tile_i]);
    maa_indirect_load<int>(&boundaries[1], nodesi_tiles[current_tile_i], boundaries1_tiles[current_tile_i], condi_res_tiles[current_tile_i]);

    // main loop
    for (; i_base < i_max; i_base += TILE_SIZE) {

        /*************** prefetch the next i tile ***************/
        int next_i_base = i_base + TILE_SIZE;
        if (next_i_base < i_max) {
            int next_tile_i = 1 - current_tile_i;
            int next_min = i_base * stride + min;
            // i = min
            maa_const(next_min, min_reg_ids[next_tile_i]);
            // load a tile of nodes[i] values to the nodesi SPD
            maa_stream_load<int>(nodes, min_reg_ids[next_tile_i], max_reg_id, stride_reg_id, nodesi_tiles[next_tile_i]);
            // load a tile of cond[nodes[i]] values to the condi SPD
            maa_indirect_load<float>(cond, nodesi_tiles[next_tile_i], condi_tiles[next_tile_i]);
            // if (cond_const < cond[i])
            maa_alu_scalar<float>(condi_tiles[next_tile_i], cond_const_reg_id, condi_res_tiles[next_tile_i], Operation_t::GT_OP);
            // load a tile of boundaries[nodes[i]] and boundaries[nodes[i] + 1] values
            // to the boundaries0 and boundaries1 SPD if (cond_const < cond[nodes[i]]) holds
            maa_indirect_load<int>(boundaries, nodesi_tiles[next_tile_i], boundaries0_tiles[next_tile_i], condi_res_tiles[next_tile_i]);
            maa_indirect_load<int>(&boundaries[1], nodesi_tiles[next_tile_i], boundaries1_tiles[next_tile_i], condi_res_tiles[next_tile_i]);
        }

        int current_tile_j = 0;
        int curr_tilej_size = 0;
        // i = 0, j = -1
        maa_const<int>(0, last_i_reg_id);
        maa_const<int>(-1, last_j_reg_id);

        /*************** prefetch the first i tile ***************/
        // generate a tile of i and j values for the current inner loop
        // iteration tile if (cond_const < cond[nodes[i]]) holds
        maa_range_loop<float>(last_i_reg_id,
                              last_j_reg_id,
                              boundaries0_tiles[current_tile_i],
                              boundaries1_tiles[current_tile_i],
                              one_reg_id,
                              i_tiles[current_tile_j],
                              j_tiles[current_tile_j],
                              condi_res_tiles[current_tile_i]);
        // load a tile of nodes[j] values to the nodesj SPD
        maa_indirect_load<int>(nodes, j_tiles[current_tile_j], nodesj_tiles[current_tile_j]);
        // load a tile of cond[nodes[j]] values to the condj SPD
        maa_indirect_load<float>(cond, nodesj_tiles[current_tile_j], condj_tiles[current_tile_j]);
        // if (cond_const < cond[j])
        maa_alu_scalar<float>(condj_tiles[current_tile_j], cond_const_reg_id, condj_res_tiles[current_tile_j], Operation_t::GT_OP);
        // load a tile of idx[j] values to the idxj SPD if (cond_const < cond[nodes[j]]) holds
        maa_indirect_load<int>(idx, j_tiles[current_tile_j], idx_tiles[current_tile_j], condj_res_tiles[current_tile_j]);
        // load a tile of b[idx[j]] values to the b SPD if (cond_const < cond[nodes[j]]) holds
        maa_indirect_load<float>(b, idx_tiles[current_tile_j], b_tiles[current_tile_j], condj_res_tiles[current_tile_j]);

        // In each iteration of this do-while loop, we generate a tile of i and j
        // values corresponding to the current tile of i values.
        do {
            /*************** prefetch the next i tile ***************/
            wait_ready(j_tiles[current_tile_j]);
            // getting the size of i and j tiles which are the number of current inner loop iterations
            curr_tilej_size = get_tile_size(j_tiles[current_tile_j]);
            if (curr_tilej_size == TILE_SIZE) {
                int next_tile_j = 1 - current_tile_j;
                // generate a tile of i and j values for the current inner loop
                // iteration tile if (cond_const < cond[nodes[i]]) holds
                maa_range_loop<float>(last_i_reg_id,
                                      last_j_reg_id,
                                      boundaries0_tiles[current_tile_i],
                                      boundaries1_tiles[current_tile_i],
                                      one_reg_id,
                                      i_tiles[next_tile_j],
                                      j_tiles[next_tile_j],
                                      condi_res_tiles[current_tile_i]);
                // load a tile of nodes[j] values to the nodesj SPD
                maa_indirect_load<int>(nodes, j_tiles[next_tile_j], nodesj_tiles[next_tile_j]);
                // load a tile of cond[nodes[j]] values to the condj SPD
                maa_indirect_load<float>(cond, nodesj_tiles[next_tile_j], condj_tiles[next_tile_j]);
                // if (cond_const < cond[j])
                maa_alu_scalar<float>(condj_tiles[next_tile_j], cond_const_reg_id, condj_res_tiles[next_tile_j], Operation_t::GT_OP);
                // load a tile of idx[j] values to the idxj SPD if (cond_const < cond[nodes[j]]) holds
                maa_indirect_load<int>(idx, j_tiles[next_tile_j], idx_tiles[next_tile_j], condj_res_tiles[next_tile_j]);
                // load a tile of b[idx[j]] values to the b SPD if (cond_const < cond[nodes[j]]) holds
                maa_indirect_load<float>(b, idx_tiles[next_tile_j], b_tiles[next_tile_j], condj_res_tiles[next_tile_j]);
            }

            // wait until the current b[idx[j]] tile is fetched to the SPD
            wait_ready(b_tiles[current_tile_j]);
            int *i_p = i_ps[current_tile_j];
            int *j_p = j_ps[current_tile_j];
            float *b_p = b_ps[current_tile_j];
            volatile float *a_p = a_ps[current_tile_j];
            float *condj_p = condj_ps[current_tile_j];
#pragma omp for // schedule(dynamic)
            for (int j = 0; j < curr_tilej_size; j++) {
                int i_offset = i_p[j];
                // Here we can calculate the real i and j values in the
                // legacy C code as follows:
                int real_i = (i_base + i_offset) * stride + min;
                int real_j = j_p[j];
                if (cond_const < condj_p[j]) {
                    a_p[j] = mul_const * b_p[j];
                }
            }

            // Read-modify-write a tile of a[idx[j]] with the values in the a SPD tile
            maa_indirect_rmw<float>(a, idx_tiles[current_tile_j], a_tiles[current_tile_j], Operation_t::ADD_OP, condj_res_tiles[current_tile_j]);
            // wait until the a[idx[j]] SPD tile is read-modified-written to the memory
            wait_ready(a_tile);
        } while (curr_tilej_size > 0);
    }

#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}