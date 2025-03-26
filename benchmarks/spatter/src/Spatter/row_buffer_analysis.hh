// #pragma once
// #include <iostream>
// #include <cassert>
// #include <random>
// #include <algorithm>
// #include <unordered_set>
// #include <queue>

// u_int findAlignedStartAddress(size_t index, size_t element_size, size_t row_size = 8192) {
//     // Calculate the address of array[index]
//     u_int element_address = (u_int)index * (u_int)element_size;

//     // Align the element address to the nearest lower multiple of row_size (8192)
//     u_int row_start_address = (element_address / (u_int)row_size) * (u_int)row_size;

//     return row_start_address;
// }

// template <int SIZE>
// class Cache {
//     std::unordered_set<u_int> cache;
//     std::queue<u_int> cache_queue;

// public:
//     void add(u_int cache_line_addr) {
//         if (cache.count(cache_line_addr) == 0) {
//             if (cache.size() == 131072) {
//                 // evice front of queue
//                 cache.erase(cache_queue.front());
//                 cache_queue.pop();
//             }
//             cache.insert(cache_line_addr);
//             cache_queue.push(cache_line_addr);
//         }
//     }
//     bool contains(u_int cache_line_addr) {
//         return cache.count(cache_line_addr) > 0;
//     }
// };

// void analysis(u_int length, u_int tile_size, u_int *idx, u_int element_size) {
//     // Evaluation: for each tile, analyze how many unique row buffers are accessed
//     std::cout << "Length is " << length << "; tile size is " << tile_size << " bytes;" << " element size " << element_size << " bytes" << std::endl;
//     tile_size = tile_size / element_size;
//     double sum_unique_row = 0;
//     double sum_unique_row_cache_miss = 0;
//     Cache<131072> cache;
//     int num_cache_misses = 0;
//     u_int num_tiles = length % tile_size == 0 ? length / tile_size : length / tile_size + 1;
//     for (u_int i = 0; i < length; i += tile_size) {
//         std::unordered_set<u_int> unique_row_buffers;
//         std::unordered_set<u_int> unique_row_buffers_cache_miss;
//         int num_cache_misses_tile = 0;
//         for (u_int j = i; j < std::min(i + tile_size, length); j++) {
//             u_int row_buffer_addr = findAlignedStartAddress(idx[j], element_size);
//             u_int cache_line_addr = findAlignedStartAddress(idx[j], element_size, 64);
//             unique_row_buffers.insert(row_buffer_addr);
//             if (!cache.contains(cache_line_addr)) {
//                 num_cache_misses_tile++;
//                 unique_row_buffers_cache_miss.insert(row_buffer_addr);
//                 cache.add(cache_line_addr);
//             }
//         }
//         num_cache_misses += num_cache_misses_tile;
//         sum_unique_row += (double)unique_row_buffers.size();
//         sum_unique_row_cache_miss += (double)unique_row_buffers_cache_miss.size();
//     }
//     std::cout << "Average elements per row " << length / sum_unique_row << std::endl;
//     std::cout << "Average elements per row (cache miss) " << num_cache_misses / sum_unique_row_cache_miss << std::endl;
//     std::cout << "Average hit rate " << (float)(length - num_cache_misses) * 100.00 / (float)(length) << std::endl;
// }