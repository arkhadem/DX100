GEM5_HOME=../..

if [ "$1" = "FUNC" ]; then
    g++ -std=c++11 test_functional.cpp -o test_functional.o -g3 -fopenmp -DFUNC -DTILE_SIZE=16384 -DNUM_CORES=4 -O3
elif [ "$1" = "GEM5" ]; then
    GEM5_INCLUDE="-I${GEM5_HOME}/include/ -I${GEM5_HOME}/util/m5/src/"
    GEM5_LIB="-L${GEM5_HOME}/util/m5/build/x86/out"
    g++ -std=c++11 $GEM5_HOME/util/m5/build/x86/abi/x86/m5op.S test_functional.cpp $GEM5_LIB $GEM5_INCLUDE -g3 -fopenmp -DGEM5 -DTILE_SIZE=16384 -DNUM_CORES=4 -O3 -o test_functional.o
else
    echo "Usage: make.sh FUNC|GEM5"
    exit 1
fi