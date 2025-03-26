GEM5_HOME=~/gem5-hpc

if [ "$1" = "FUNC" ]; then
    # g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx test.cpp -o test_T1K.o -g3 -fopenmp -DFUNC -DTILE_SIZE=1024 -O3
    # g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx test.cpp -o test_T2K.o -g3 -fopenmp -DFUNC -DTILE_SIZE=2048 -O3
    # g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx test.cpp -o test_T4K.o -g3 -fopenmp -DFUNC -DTILE_SIZE=4096 -O3
    # g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx test.cpp -o test_T8K.o -g3 -fopenmp -DFUNC -DTILE_SIZE=8192 -O3
    g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx test.cpp -o test_T16K.o -g3 -fopenmp -DFUNC -DTILE_SIZE=16384 -O3
    # g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx test_double.cpp -o test_double_T1K.o -g3 -fopenmp -DFUNC -DTILE_SIZE=1024 -O3
    # g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx test_double.cpp -o test_double_T2K.o -g3 -fopenmp -DFUNC -DTILE_SIZE=2048 -O3
    # g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx test_double.cpp -o test_double_T4K.o -g3 -fopenmp -DFUNC -DTILE_SIZE=4096 -O3
    # g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx test_double.cpp -o test_double_T8K.o -g3 -fopenmp -DFUNC -DTILE_SIZE=8192 -O3
    # g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx test_double.cpp -o test_double_T16K.o -g3 -fopenmp -DFUNC -DTILE_SIZE=16384 -O3
    g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx -D$1 -fPIC -c MAA_compiler_api.cpp -g3 -fopenmp -o  MAA_compiler_api.o
    ar rcs libmaacompiler.a MAA_compiler_api.o
elif [ "$1" = "GEM5" ]; then
    GEM5_INCLUDE="-I${GEM5_HOME}/include/ -I${GEM5_HOME}/util/m5/src/"
    GEM5_LIB="-L${GEM5_HOME}/util/m5/build/x86/out"
# g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx 
#     -O3 -fopenmp -DGEM5 -DTILE_SIZE=16384 \
#     -S test.cpp -o test2.s $GEM5_INCLUDE -fopt-info-vec-optimized -ftree-vectorizer-verbose=2
    g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx test.cpp $GEM5_LIB $GEM5_INCLUDE -fopenmp -DGEM5 -DTILE_SIZE=16384 -O3 -S -o test_T16K.s
    # g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx $GEM5_HOME/util/m5/build/x86/abi/x86/m5op.S test.cpp $GEM5_LIB $GEM5_INCLUDE -g3 -fopenmp -DGEM5 -DTILE_SIZE=1024 -O3 -o test_T1K.o
    # g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx $GEM5_HOME/util/m5/build/x86/abi/x86/m5op.S test.cpp $GEM5_LIB $GEM5_INCLUDE -g3 -fopenmp -DGEM5 -DTILE_SIZE=2048 -O3 -o test_T2K.o
    # g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx $GEM5_HOME/util/m5/build/x86/abi/x86/m5op.S test.cpp $GEM5_LIB $GEM5_INCLUDE -g3 -fopenmp -DGEM5 -DTILE_SIZE=4096 -O3 -o test_T4K.o
    # g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx $GEM5_HOME/util/m5/build/x86/abi/x86/m5op.S test.cpp $GEM5_LIB $GEM5_INCLUDE -g3 -fopenmp -DGEM5 -DTILE_SIZE=8192 -O3 -o test_T8K.o
    g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx $GEM5_HOME/util/m5/build/x86/abi/x86/m5op.S test.cpp $GEM5_LIB $GEM5_INCLUDE -g3 -fopenmp -DGEM5 -DTILE_SIZE=16384 -O3 -o test_T16K.o
    # g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx $GEM5_HOME/util/m5/build/x86/abi/x86/m5op.S test_double.cpp $GEM5_LIB $GEM5_INCLUDE -g3 -fopenmp -DGEM5 -DTILE_SIZE=1024 -O3 -o test_double_T1K.o
    # g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx $GEM5_HOME/util/m5/build/x86/abi/x86/m5op.S test_double.cpp $GEM5_LIB $GEM5_INCLUDE -g3 -fopenmp -DGEM5 -DTILE_SIZE=2048 -O3 -o test_double_T2K.o
    # g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx $GEM5_HOME/util/m5/build/x86/abi/x86/m5op.S test_double.cpp $GEM5_LIB $GEM5_INCLUDE -g3 -fopenmp -DGEM5 -DTILE_SIZE=4096 -O3 -o test_double_T4K.o
    # g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx $GEM5_HOME/util/m5/build/x86/abi/x86/m5op.S test_double.cpp $GEM5_LIB $GEM5_INCLUDE -g3 -fopenmp -DGEM5 -DTILE_SIZE=8192 -O3 -o test_double_T8K.o
    # g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx $GEM5_HOME/util/m5/build/x86/abi/x86/m5op.S test_double.cpp $GEM5_LIB $GEM5_INCLUDE -g3 -fopenmp -DGEM5 -DTILE_SIZE=16384 -O3 -o test_double_T16K.o
    g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx -c $GEM5_HOME/util/m5/build/x86/abi/x86/m5op.S  $GEM5_LIB $GEM5_INCLUDE -o m5op.o
    g++ -std=c++11 -march=corei7 -msse4.1 -mno-avx -c MAA_compiler_api.cpp $GEM5_LIB $GEM5_INCLUDE -D$1 -fPIC -g3 -fopenmp -o MAA_compiler_api.o
    ar rcs libmaacompiler.a m5op.o MAA_compiler_api.o
else
    echo "Usage: make.sh FUNC|GEM5"
    exit 1
fi
