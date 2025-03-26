GEM5_HOME=~/gem5-hpc

if [ "$1" = "FUNC" ]; then
    g++-12 -std=c++11 test_double.cpp -o test_double_T16K.o -g3 -fopenmp -DFUNC -DTILE_SIZE=16384 -DNUM_CORES=4 -O3
    g++-12 -std=c++11 test_double.cpp -o test_double_4C.o -g3 -fopenmp -DFUNC -DTILE_SIZE=16384 -DNUM_CORES=4 -O3
    g++-12 -std=c++11 test_double.cpp -o test_double_8C.o -g3 -fopenmp -DFUNC -DTILE_SIZE=16384 -DNUM_CORES=8 -O3
    g++-12 -std=c++11 test_double.cpp -o test_double_16C.o -g3 -fopenmp -DFUNC -DTILE_SIZE=16384 -DNUM_CORES=16 -O3
    g++-12 -std=c++11 test_double.cpp -o test_double_32C.o -g3 -fopenmp -DFUNC -DTILE_SIZE=16384 -DNUM_CORES=32 -O3
    g++-12 -std=c++11 -D$1 -fPIC -c MAA_compiler_api.cpp -g3 -fopenmp -o  MAA_compiler_api.o
    ar rcs libmaacompiler.a MAA_compiler_api.o
elif [ "$1" = "GEM5" ]; then
    GEM5_INCLUDE="-I${GEM5_HOME}/include/ -I${GEM5_HOME}/util/m5/src/"
    GEM5_LIB="-L${GEM5_HOME}/util/m5/build/x86/out"
    g++-12 -std=c++11 $GEM5_HOME/util/m5/build/x86/abi/x86/m5op.S test_double.cpp $GEM5_LIB $GEM5_INCLUDE -g3 -fopenmp -DGEM5 -DTILE_SIZE=16384 -O3 -o test_double_T16K.o
    g++-12 -std=c++11 $GEM5_HOME/util/m5/build/x86/abi/x86/m5op.S test_double.cpp $GEM5_LIB $GEM5_INCLUDE -g3 -fopenmp -DGEM5 -DTILE_SIZE=16384 -DNUM_CORES=4 -O3 -o test_double_4C.o
    g++-12 -std=c++11 $GEM5_HOME/util/m5/build/x86/abi/x86/m5op.S test_double.cpp $GEM5_LIB $GEM5_INCLUDE -g3 -fopenmp -DGEM5 -DTILE_SIZE=16384 -DNUM_CORES=8 -O3 -o test_double_8C.o
    g++-12 -std=c++11 $GEM5_HOME/util/m5/build/x86/abi/x86/m5op.S test_double.cpp $GEM5_LIB $GEM5_INCLUDE -g3 -fopenmp -DGEM5 -DTILE_SIZE=16384 -DNUM_CORES=16 -O3 -o test_double_16C.o
    g++-12 -std=c++11 $GEM5_HOME/util/m5/build/x86/abi/x86/m5op.S test_double.cpp $GEM5_LIB $GEM5_INCLUDE -g3 -fopenmp -DGEM5 -DTILE_SIZE=16384 -DNUM_CORES=32 -O3 -o test_double_32C.o
    g++-12 -std=c++11 -c $GEM5_HOME/util/m5/build/x86/abi/x86/m5op.S  $GEM5_LIB $GEM5_INCLUDE -o m5op.o
    g++-12 -std=c++11 -c MAA_compiler_api.cpp $GEM5_LIB $GEM5_INCLUDE -D$1 -fPIC -g3 -fopenmp -o MAA_compiler_api.o
    ar rcs libmaacompiler.a m5op.o MAA_compiler_api.o
else
    echo "Usage: make.sh FUNC|GEM5"
    exit 1
fi
