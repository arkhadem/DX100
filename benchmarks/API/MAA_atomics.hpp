#ifndef MAA_ATOMICS_HPP_
#define MAA_ATOMICS_HPP_

#if defined _OPENMP

#if defined __GNUC__

// gcc/clang/icc instrinsics
template <typename T, typename U>
T fetch_and_add(T *x, U inc) {
    return __atomic_fetch_add(x, inc);
}

template <typename T, typename U>
T fetch_and_sub(T *x, U inc) {
    return atomic_fetch_sub(x, inc);
}

#else // defined __GNUC__ __SUNPRO_CC

#error No atomics available for this compiler but using OpenMP

#endif // else defined __GNUC__ __SUNPRO_CC

#else // defined _OPENMP

// serial fallbacks

template <typename T, typename U>
T fetch_and_add(T x, U inc) {
    T orig_val = (*x);
    (*x) += inc;
    return orig_val;
}

template <typename T, typename U>
T fetch_and_sub(T x, U inc) {
    T orig_val = (*x);
    (*x) -= inc;
    return orig_val;
}

#endif // else defined _OPENMP

#endif // MAA_ATOMICS_HPP_
