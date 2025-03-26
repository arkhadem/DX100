#pragma once

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

#include <pthread.h>
#define MAA_CRITICAL_INIT pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#define MAA_CRITICAL_START pthread_mutex_lock(&mutex);
#define MAA_CRITICAL_END pthread_mutex_unlock(&mutex);
// template <int NUM>
// inline int get_const_tile(){
//     int SPD_id = get_new_tile<int>();
//     int *tile = get_cacheable_tile_pointer<int>(SPD_id);
//     for (int i = 0; i < TILE_SIZE; i++) {
//         tile[i] = NUM;
//     }
//     set_tile_size(SPD_id, TILE_SIZE);
//     return SPD_id;
// }