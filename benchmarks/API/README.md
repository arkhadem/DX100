# MAA Mirobenchmarks and Functional Simulator

This repo contains the functional simulator and tests for the MAA programming model.

## Content

- `test.cpp`: the CPU and MAA version of various tests, heavily commented.
- `MAA.hpp`: the utility APIs for manipulating scratchpad (SPD) and scalar registers of MAA.
- `MAA_functional.hpp`: the functional simulator.
- `MAA_gem5.hpp` and `MAA_gem5_magic.hpp`: call the pseudo M5 instructions for GEM5 simulation.

## Setup

Compile the test using:

```bash
bash make.sh [FUNC | GEM5 | GEM5_MAGIC]
```

- `FUNC` will use the functional (`MAA_functional.hpp`).
- `GEM5` and `GEM5_MAGIC` implements the APIs using the M5 pseudo instructions that will be added in phase 1 and 3 of the project (`MAA_gem5.hpp` and `MAA_gem5_magic.hpp`)

**IMPORTANT:** If you are compiling with `GEM5` or `GEM5_MAGIC` flags, make sure you have already set up the GEM5, compiled it, and set the `GEM5_HOME` in `make.sh` to the GEM5 path correctly.

## How to rewrite a kernel using MAA APIs?

### Single loop

If the loop does not start from `0` with the stride of `1`, *e.g.*:

```C++
for (int i = min; i < max; i += stride) {
}
```

Convert it to:

```C++
int i_max = (max - min - 1) / stride + 1;
for (int i = 0; i < i_max; i++) {
    int real_i = i * stride + min;
}
```

Where new `i` iterates from `0` to `i_max` with `stride = 1`.
If needed, you can calculate the value of previous `i` iterator (`real_i`) which iterates from `min` to `max` with `stride`.

For tiling, convert the previous loop into 2 nested loops.
The outer loop iterates on tiles, and the inner loop iterates on the individual iterations of the tile.

```C++
int i_max = (max - min - 1) / stride + 1;
for (int i_base = 0; i_base < i_max; i += TILE_SIZE) {
    for (int i_offset = 0; i_offset < curr_tile_size; i_offset++) {
        int curr_tile_size = min(i_max - i_base, TILE_SIZE);
        for (int i_offset = 0; i_offset < curr_tile_size; i_offset++) {
            int i = i_base + i_offset;
            int real_i = i * stride + min;
        }
    }
}
```

### Single loop + direct range loop

For example:

```C++
for (int i = min; i < max; i += stride) {
    for (int j = boundaries[i]; j < boundaries[i + 1]; j++) {
    }
}
```

First, convert the outer loop to a new outer loop iterating from `0` to `i_max` with `stride = 1`:

```C++
int i_max = ceil((max - min) / stride);
for (int i = 0; i < i_max; i++) {
    int real_i = i * stride + min;
    for (int j = boundaries[real_i]; j < boundaries[real_i + 1]; j++) {
    }
}
```

Second, tile the outer loop:

```C++
int i_max = ceil((max - min) / stride);
for (int i_base = 0; i_base < i_max; i += TILE_SIZE) {
    int curr_tile_size = min(i_max - i_base, TILE_SIZE);
    for (int i_offset = 0; i_offset < curr_tile_size; i_offset++) {
        int i = i_base + i_offset;
        int real_i = i * stride + min;
        for (int j = boundaries[real_i]; j < boundaries[real_i + 1]; j++) {
        }
    }
}
```

Third, we merge the `i_offset` and `j` loops into a single loop. For that, we need a preprocessing step which goes over the previous two i_of`i_offset` and `j` loops and stores the `i_offset` and `j` iterator values to two arrays:

```C++
int i_max = ceil((max - min) / stride);
for (int i_base = 0; i_base < i_max; i += TILE_SIZE) {
    int curr_tile_size = min(i_max - i_base, TILE_SIZE);

    // preprocessing step, will be accelerated by maa_range_loop instruction
    std::vector<int> i_values, j_values;
    for (int i_offset = 0; i_offset < curr_tile_size; i_offset++) {
        int i = i_base + i_offset;
        int real_i = i * stride + min;
        for (int j = boundaries[real_i]; j < boundaries[real_i + 1]; j++) {
            i_values.push_back(i_offset);
            j_values.push_back(j);
        }
    }

    for (int idx = 0; idx < i_values.size(); idx++) {
        int i_offset = i_values[idx];
        int real_i = (i_base + i_offset) * stride + min;
        int real_j = j_values[idx];
    }
}
```

Finally, we accelerate the preprocessing step using the `maa_range_loop` instruction.
This instruction will generate a tile of i and j values per call.

```C++
int i_max = ceil((max - min) / stride);
for (int i_base = 0; i_base < i_max; i += TILE_SIZE) {
    int curr_tile_size = min(i_max - i_base, TILE_SIZE);

    // load a tile of boundaries[i] and boundaries[i + 1] values to boundaries0 and boundaries1 SPD
    maa_stream_load<int, int>(boundaries, min_reg_id, max_reg_id, stride_reg_id, boundaries0_tile);
    maa_stream_load<int, int>(&boundaries[1], min_reg_id, max_reg_id, stride_reg_id, boundaries1_tile);

    // i = 0, j = -1
    maa_const<int>(0, last_i_reg_id);
    maa_const<int>(-1, last_j_reg_id);
    do {
        // generate a tile of i and j values for the current inner loop iteration tile
        maa_range_loop<float>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile);
        curr_tilej_size = get_tile_size(j_tile);
        for (int j = 0; j < curr_tilej_size; j++) {
            int i_offset = i_p[j];
            int real_i = (i_base + i_offset) * stride + min;
            int real_j = j_p[j];
        }
    } while (curr_tilej_size > 0);
}
```

### Single loop + indirect range loop

For example:

```C++
for (int i = min; i < max; i += stride) {
    for (int j = boundaries[nodes[i]]; j < boundaries[nodes[i] + 1]; j++) {
    }
}
```

It's similar to "single loop + direct range loop", with the only difference that the boundary arrays are loaded with indirect memory accesses, *i.e.*:

```C++
int i_max = ceil((max - min) / stride);
for (int i_base = 0; i_base < i_max; i += TILE_SIZE) {
    int curr_tile_size = min(i_max - i_base, TILE_SIZE);

    // load a tile of nodes[i] values to the nodes SPD
    maa_stream_load<int, float>(nodes, min_reg_id, max_reg_id, stride_reg_id, nodes_tile);
    // load a tile of boundaries[nodes[i]] and boundaries[nodes[i] + 1] values to boundaries0 and boundaries1 SPD
    maa_stream_load<int, int>(boundaries, min_reg_id, max_reg_id, stride_reg_id, boundaries0_tile);
    maa_stream_load<int, int>(&boundaries[1], min_reg_id, max_reg_id, stride_reg_id, boundaries1_tile);

    // i = 0, j = -1
    maa_const<int>(0, last_i_reg_id);
    maa_const<int>(-1, last_j_reg_id);
    do {
        // generate a tile of i and j values for the current inner loop iteration tile
        maa_range_loop<float>(last_i_reg_id, last_j_reg_id, boundaries0_tile, boundaries1_tile, one_reg_id, i_tile, j_tile);
        curr_tilej_size = get_tile_size(j_tile);
        for (int j = 0; j < curr_tilej_size; j++) {
            int i_offset = i_p[j];
            int real_i = (i_base + i_offset) * stride + min;
            int real_j = j_p[j];
        }
    } while (curr_tilej_size > 0);
}
```