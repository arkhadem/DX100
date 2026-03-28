GEM5_HOME="${GEM5_HOME:-../..}"
M5OPS_X86="${GEM5_HOME}/util/m5/src/abi/x86/m5op.S"
COMMON_FLAGS="-std=c++11 -g3 -fopenmp -DTILE_SIZE=16384 -DNUM_CORES=4 -O3"

if [ "$1" != "FUNC" ] && [ "$1" != "GEM5" ]; then
    echo "Usage: bash make_benchmark_indir_ld_rep.sh FUNC|GEM5"
    exit 1
fi

if [ "$1" = "FUNC" ]; then
    g++ ${COMMON_FLAGS} -DFUNC benchmark_indir_ld_rep.cpp -o benchmark_indir_ld_rep.o
    exit $?
fi

if [ ! -f "${M5OPS_X86}" ]; then
    echo "Could not find m5op.S at ${M5OPS_X86}"
    echo "Set GEM5_HOME to your gem5 tree root and retry."
    exit 1
fi

GEM5_INCLUDE="-I${GEM5_HOME}/include/ -I${GEM5_HOME}/util/m5/src/"
g++ ${COMMON_FLAGS} -DGEM5 "${M5OPS_X86}" benchmark_indir_ld_rep.cpp ${GEM5_INCLUDE} -o benchmark_indir_ld_rep.o
