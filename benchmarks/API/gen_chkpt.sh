OUTDIR=./chkpt
GEM5_HOME=~/gem5-hpc
OMP_PROC_BIND=false OMP_NUM_THREADS=4 ${GEM5_HOME}/build/X86/gem5.fast --outdir=$OUTDIR ${GEM5_HOME}/configs/deprecated/example/se.py --cpu-type AtomicSimpleCPU -n 4 --mem-size "16GB" --cmd ./test_T16K.o  --options "16384 1 MAA gather" 2>&1 | awk '{ print strftime(), $0; fflush() }' | tee $OUTDIR/logs_checkpoint.txt;