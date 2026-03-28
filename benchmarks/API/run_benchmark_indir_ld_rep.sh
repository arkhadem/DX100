GEM5_HOME="${GEM5_HOME:-../..}"

N=${1:-64}
DEPTH=${2:-3}
MODE=${3:-CMP}
OUTDIR="./chkpt_indir_ld_rep_${MODE}_N${N}_D${DEPTH}/"

mkdir -p "${OUTDIR}"

if [ ! -x "${GEM5_HOME}/build/X86/gem5.opt" ]; then
    echo "Could not find gem5.opt at ${GEM5_HOME}/build/X86/gem5.opt"
    echo "Set GEM5_HOME to your built gem5 tree root and retry."
    exit 1
fi

if [ ! -x ./benchmark_indir_ld_rep.o ]; then
    echo "Could not find ./benchmark_indir_ld_rep.o"
    echo "Build it first with: bash make_benchmark_indir_ld_rep.sh GEM5"
    exit 1
fi

if [ "${MODE}" != "BASE" ] && [ "${MODE}" != "MAA" ] && [ "${MODE}" != "CMP" ]; then
    echo "Usage: bash run_benchmark_indir_ld_rep.sh [n] [depth] [BASE|MAA|CMP]"
    exit 1
fi

OMP_PROC_BIND=false OMP_NUM_THREADS=4 ${GEM5_HOME}/build/X86/gem5.opt --outdir=${OUTDIR} ${GEM5_HOME}/configs/deprecated/example/se.py --cpu-type X86O3CPU -n 4 --mem-size '16GB' --sys-clock '3.2GHz' --cpu-clock '3.2GHz' --caches --l1d_size=32kB --l1d_assoc=8 --l1d-hwp-type=StridePrefetcher --l1d_mshrs=16 --l1i_size=32kB --l1i_assoc=8 --l1i-hwp-type=StridePrefetcher --l1i_mshrs=16 --l2cache --l2_size=256kB --l2_assoc=4 --l2-hwp-type=StridePrefetcher --l2_mshrs=32 --l3cache --l3_size=8MB --l3_assoc=16 --l3_mshrs=256 --cacheline_size=64 --mem-type Ramulator2 --ramulator-config ${GEM5_HOME}/ext/ramulator2/ramulator2/example_gem5_config.yaml --mem-channels 1 --maa --maa_num_tile_elements 16384 --cmd ./benchmark_indir_ld_rep.o --options "${N} ${DEPTH} ${MODE}" -r 1 --prog-interval=1000 2>&1 | awk '{ print strftime(), $0; fflush() }' | tee ${OUTDIR}/logs_trace.txt
