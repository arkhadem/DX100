SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

GEM5_HOME="${GEM5_HOME:-$(cd "$SCRIPT_DIR/../.." && pwd)}"

N=${1:-64}
DEPTH=${2:-3}
MODE=${3:-CMP}
OUTDIR="./chkpt_indir_ld_rep_${MODE}_N${N}_D${DEPTH}/"

mkdir -p "${OUTDIR}"

# Require a real ELF executable; gem5.opt may exist but be non-executable or corrupt.
pick_gem5_bin() {
    local f="$1"
    [ -f "$f" ] && [ -x "$f" ] && file -b "$f" 2>/dev/null | grep -q "ELF.*executable"
}

GEM5_OPT="${GEM5_HOME}/build/X86/gem5.opt"
GEM5_FAST="${GEM5_HOME}/build/X86/gem5.fast"
if pick_gem5_bin "${GEM5_OPT}"; then
    GEM5_BIN="${GEM5_OPT}"
elif pick_gem5_bin "${GEM5_FAST}"; then
    echo "Using gem5.fast (gem5.opt missing, not executable, or not a valid ELF binary)."
    GEM5_BIN="${GEM5_FAST}"
else
    echo "No usable gem5 binary. Checked:"
    echo "  ${GEM5_OPT}"
    echo "  ${GEM5_FAST}"
    [ -f "${GEM5_OPT}" ] && [ ! -x "${GEM5_OPT}" ] && echo "Hint: chmod +x ${GEM5_OPT} only helps if the file is a real ELF (rebuild with: scons build/X86/gem5.opt)."
    exit 1
fi

if [ ! -e ./benchmark_indir_ld_rep.o ]; then
    echo "Could not find ./benchmark_indir_ld_rep.o"
    echo "Build it first with: bash make_benchmark_indir_ld_rep.sh GEM5"
    exit 1
fi

if [ "${MODE}" != "BASE" ] && [ "${MODE}" != "MAA" ] && [ "${MODE}" != "CMP" ]; then
    echo "Usage: bash run_benchmark_indir_ld_rep.sh [n] [depth] [BASE|MAA|CMP]"
    exit 1
fi

OMP_PROC_BIND=false OMP_NUM_THREADS=4 ${GEM5_HOME}/build/X86/gem5.opt --outdir=${OUTDIR} ${GEM5_HOME}/configs/deprecated/example/se.py --cpu-type X86O3CPU -n 4 --mem-size '16GB' --sys-clock '3.2GHz' --cpu-clock '3.2GHz' --caches --l1d_size=32kB --l1d_assoc=8 --l1d-hwp-type=StridePrefetcher --l1d_mshrs=16 --l1i_size=32kB --l1i_assoc=8 --l1i-hwp-type=StridePrefetcher --l1i_mshrs=16 --l2cache --l2_size=256kB --l2_assoc=4 --l2-hwp-type=StridePrefetcher --l2_mshrs=32 --l3cache --l3_size=8MB --l3_assoc=16 --l3_mshrs=256 --cacheline_size=64 --mem-type Ramulator2 --ramulator-config ${GEM5_HOME}/ext/ramulator2/ramulator2/example_gem5_config.yaml --mem-channels 1 --maa --maa_num_tile_elements 16384 --cmd ./benchmark_indir_ld_rep.o --options "${N} ${DEPTH} ${MODE}" --prog-interval=1000 2>&1 | awk '{ print strftime(), $0; fflush() }' | tee ${OUTDIR}/logs_trace.txt
