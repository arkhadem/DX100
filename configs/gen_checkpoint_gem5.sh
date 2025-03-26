# --debug-flags=Exec

if [ -n "$3" ]
then
    export OMP_PROC_BIND=false; export OMP_NUM_THREADS=4; ~/gem5-hpc/build/X86/gem5.fast --outdir=$1 ~/gem5-hpc/configs/deprecated/example/se.py --cpu-type AtomicSimpleCPU -n 4 --mem-size '16GB' --cmd "\"$2\"" --options "\"$3\""
else
    export OMP_PROC_BIND=false; export OMP_NUM_THREADS=4; ~/gem5-hpc/build/X86/gem5.fast --outdir=$1 ~/gem5-hpc/configs/deprecated/example/se.py --cpu-type AtomicSimpleCPU -n 4 --mem-size '16GB' --cmd "\"$2\""
fi